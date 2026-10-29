//===================================================================
// ncdf_ncio.cpp —— NetCDF 读取层
// 由 ncdf.cpp 拆出（2026-10-28）。纯代码搬移，无逻辑改动。
// 这里只管「读文件」：属性/维度发现、元数据、逐时次读取。
// UI 与事件处理留在 ncdf.cpp。
//===================================================================
#include "stdio.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "wx/wx.h"
#include "ncdf.h"
#include "ncdf_reader.h"
#include "ncdfdata.h"
#include "ncdf_pi.h"
#include "ncdfoverlayfactory.h"

extern void ncdfLog(const char* format, ...);

/* Helper: read a text attribute from a variable. Caller must delete[] the result. */
static char* read_var_att_text(int ncid, int varid, const char* attname) {
    nc_type xtype;
    if (nc_inq_atttype(ncid, varid, attname, &xtype) != NC_NOERR) return NULL;

    // 【NetCDF-4 的字符串属性是 NC_STRING（变长），不是 NC_CHAR】
    // 用 nc_inq_attlen + nc_get_att_text 读会得到 attlen=1（串的个数）并以
    // 类型不匹配失败返回 NULL，导致 standard_name 全部读不出来，
    // 只能退到变量名匹配（2026-10-29 下载 AVISO L4 后实测踩到）。
    if (xtype == NC_STRING) {
        size_t n = 0;
        if (nc_inq_attlen(ncid, varid, attname, &n) != NC_NOERR || n == 0) return NULL;
        // nc_get_att_string 的参数是 char** —— 调用方分配指针数组，库分配每个串
        char **vals = new char*[n];
        if (nc_get_att_string(ncid, varid, attname, vals) != NC_NOERR) {
            delete[] vals;
            return NULL;
        }
        size_t len = (vals[0] && vals[0][0]) ? strlen(vals[0]) : 0;
        char *text = new char[len + 1];
        if (len) memcpy(text, vals[0], len);
        text[len] = '\0';
        nc_free_string(n, vals);
        delete[] vals;
        return text;
    }

    size_t attlen;
    if (nc_inq_attlen(ncid, varid, attname, &attlen) != NC_NOERR) return NULL;
    char* text = new char[attlen + 1];
    if (nc_get_att_text(ncid, varid, attname, text) != NC_NOERR) {
        delete[] text;
        return NULL;
    }
    text[attlen] = '\0';
    return text;
}

/* CF-compliant variable finder with three-tier priority:
   P1 (gold standard): exact standard_name match
   P2 (disambiguation): if multiple matches, return first with warning
   P3 (fallback): try long_name, then variable name, with WARNING log */
static int find_var_by_cf(int ncid,
                           const char* target_std_name,
                           const char** alt_std_names,
                           const char** long_name_hints,
                           const char** var_name_hints) {
    int nvars;
    if (nc_inq(ncid, NULL, &nvars, NULL, NULL) != NC_NOERR) return -1;
    
    /* ===== Priority 1: standard_name exact match ===== */
    int best_varid = -1;
    int match_count = 0;
    
    for (int i = 0; i < nvars; i++) {
        char* std_name = read_var_att_text(ncid, i, "standard_name");
        if (!std_name) continue;
        
        if (strcmp(std_name, target_std_name) == 0) {
            if (best_varid == -1) best_varid = i;
            match_count++;
        }
        delete[] std_name;
    }
    
    if (match_count == 1) return best_varid;
    
    /* ===== Priority 2: multiple matches ===== */
    if (match_count > 1) {
        ncdfLog("[ncdf] find_var_by_cf: WARNING - %d vars with standard_name='%s', using first (varid=%d)\n",
                match_count, target_std_name, best_varid);
        return best_varid;
    }
    
    /* ===== Try alternative standard_names ===== */
    if (alt_std_names) {
        for (const char** alt = alt_std_names; *alt; alt++) {
            for (int i = 0; i < nvars; i++) {
                char* std_name = read_var_att_text(ncid, i, "standard_name");
                if (!std_name) continue;
                if (strcmp(std_name, *alt) == 0) {
                    delete[] std_name;
                    ncdfLog("[ncdf] find_var_by_cf: matched alt standard_name='%s' (target was '%s')\n",
                            *alt, target_std_name);
                    return i;
                }
                delete[] std_name;
            }
        }
    }
    
    /* ===== Priority 3a: fallback to long_name ===== */
    if (long_name_hints) {
        for (const char** hint = long_name_hints; *hint; hint++) {
            for (int i = 0; i < nvars; i++) {
                char* long_name = read_var_att_text(ncid, i, "long_name");
                if (long_name && strcmp(long_name, *hint) == 0) {
                    ncdfLog("[ncdf] find_var_by_cf: WARNING - no standard_name, matched long_name='%s' for target='%s'\n",
                            *hint, target_std_name);
                    delete[] long_name;
                    return i;
                }
                if (long_name) delete[] long_name;
            }
        }
    }
    
    /* ===== Priority 3b: fallback to variable name ===== */
    if (var_name_hints) {
        for (const char** hint = var_name_hints; *hint; hint++) {
            int varid;
            if (nc_inq_varid(ncid, *hint, &varid) == NC_NOERR) {
                ncdfLog("[ncdf] find_var_by_cf: WARNING - no standard_name, matched variable name='%s' for target='%s'\n",
                        *hint, target_std_name);
                return varid;
            }
        }
    }
    
    return -1;
}

static int find_dim_by_var(int ncid, int varid, int dim_idx) {
    int ndims;
    nc_inq_varndims(ncid, varid, &ndims);
    if (dim_idx >= ndims) return -1;

    int* dimids = new int[ndims];
    nc_inq_var(ncid, varid, NULL, NULL, NULL, dimids, NULL);
    int dimid = dimids[dim_idx];
    delete[] dimids;
    return dimid;
}

/* Shared dimension discovery for any NetCDF variable.
   Matches variable dimensions to time/lat/lon/depth by comparing dimension IDs.
   Returns indices within the variable's dimension array. */
struct VarDimInfo {
    int time_idx, lat_idx, lon_idx, depth_idx;
    bool lat_before_lon;
    bool valid;
};

static VarDimInfo discover_var_dims(int ncid, int varid) {
    VarDimInfo info = {-1, -1, -1, -1, false, false};

    int ndims;
    if (nc_inq_varndims(ncid, varid, &ndims) != NC_NOERR) return info;

    int* dimids = (int*)calloc(ndims, sizeof(int));
    if (!dimids) return info;
    if (nc_inq_var(ncid, varid, NULL, NULL, NULL, dimids, NULL) != NC_NOERR) {
        free(dimids);
        return info;
    }

    int time_dimid = -1, lat_dimid = -1, lon_dimid = -1, depth_dimid = -1;

    nc_inq_dimid(ncid, "time", &time_dimid);

    if (nc_inq_dimid(ncid, "latitude", &lat_dimid) != NC_NOERR)
        if (nc_inq_dimid(ncid, "lat", &lat_dimid) != NC_NOERR)
            if (nc_inq_dimid(ncid, "y", &lat_dimid) != NC_NOERR)
                nc_inq_dimid(ncid, "nav_lat", &lat_dimid);

    if (nc_inq_dimid(ncid, "longitude", &lon_dimid) != NC_NOERR)
        if (nc_inq_dimid(ncid, "lon", &lon_dimid) != NC_NOERR)
            if (nc_inq_dimid(ncid, "x", &lon_dimid) != NC_NOERR)
                nc_inq_dimid(ncid, "nav_lon", &lon_dimid);

    if (nc_inq_dimid(ncid, "depth", &depth_dimid) != NC_NOERR)
        if (nc_inq_dimid(ncid, "z", &depth_dimid) != NC_NOERR)
            if (nc_inq_dimid(ncid, "deptht", &depth_dimid) != NC_NOERR)
                nc_inq_dimid(ncid, "depthu", &depth_dimid);

    for (int i = 0; i < ndims; i++) {
        if (dimids[i] == time_dimid) info.time_idx = i;
        else if (dimids[i] == lat_dimid) info.lat_idx = i;
        else if (dimids[i] == lon_dimid) info.lon_idx = i;
        else if (dimids[i] == depth_dimid) info.depth_idx = i;
    }

    free(dimids);

    if (info.time_idx == -1 || info.lat_idx == -1 || info.lon_idx == -1) {
        ncdfLog("[ncdf] discover_var_dims: FAILED for varid=%d (time=%d lat=%d lon=%d)\n",
            varid, info.time_idx, info.lat_idx, info.lon_idx);
        return info;
    }

    info.lat_before_lon = (info.lat_idx < info.lon_idx);
    info.valid = true;

    ncdfLog("[ncdf] discover_var_dims: varid=%d time=%d lat=%d lon=%d depth=%d lat_before_lon=%d\n",
        varid, info.time_idx, info.lat_idx, info.lon_idx, info.depth_idx, (int)info.lat_before_lon);

    return info;
}

/* CF-compliant time unit parser.
   Parses strings like "hours since 1950-01-01 00:00:00" or "days since 1950-01-01".
   Returns seconds_per_unit and the epoch datetime. */
struct CFTimeInfo {
    wxDateTime baseDateTime;
    double secondsPerUnit;
    bool valid;
    CFTimeInfo() : secondsPerUnit(0.0), valid(false) {}
};

static CFTimeInfo ParseCFTimeUnits(const char* unitsStr) {
    CFTimeInfo info;
    wxString units(unitsStr);
    
    int sincePos = units.Find(" since ");
    if (sincePos == wxNOT_FOUND) return info;
    
    wxString unitPart = units.Left(sincePos).Trim().Lower();
    wxString datePart = units.Mid(sincePos + 7).Trim();
    
    if (unitPart.Contains("second") || unitPart == "secs" || unitPart == "sec" || unitPart == "s")
        info.secondsPerUnit = 1.0;
    else if (unitPart.Contains("minute") || unitPart == "mins" || unitPart == "min")
        info.secondsPerUnit = 60.0;
    else if (unitPart.Contains("hour") || unitPart == "hrs" || unitPart == "hr" || unitPart == "h")
        info.secondsPerUnit = 3600.0;
    else if (unitPart.Contains("day") || unitPart == "d")
        info.secondsPerUnit = 86400.0;
    else
        return info;
    
    /* Strip trailing timezone (e.g. " UTC", " +00:00", "Z") and fractional seconds */
    wxString cleanDate;
    for (size_t j = 0; j < datePart.Len(); j++) {
        wxChar c = datePart[j];
        if (c == 'Z' || c == 'z') continue;  // skip Z
        if (c == '+' || c == '-') {
            /* Check if this is a timezone offset like "+00:00" or "-05:00" */
            if (j > 0 && datePart[j-1] == ' ') {
                break;  // timezone after space
            }
            if (j > 0 && datePart[j-1] == 'T') {
                /* This could be a negative timezone like T-05:00 */
                cleanDate += c;
                continue;
            }
        }
        cleanDate += c;
    }
    /* Remove trailing whitespace */
    cleanDate = cleanDate.Trim();
    
    if (info.baseDateTime.ParseFormat(cleanDate, "%Y-%m-%d %H:%M:%S") ||
        info.baseDateTime.ParseFormat(cleanDate, "%Y-%m-%dT%H:%M:%S") ||
        info.baseDateTime.ParseFormat(cleanDate, "%Y-%m-%d") ||
        info.baseDateTime.ParseFormat(cleanDate, "%Y-%m-%dT%H:%M:%SZ") ||
        info.baseDateTime.ParseFormat(cleanDate, "%Y-%m-%dT%H:%M:%S%z") ||
        info.baseDateTime.ParseFormat(cleanDate, "%Y-%m-%d %H:%M:%S %z")) {
        info.valid = true;
    }
    
    return info;
}

// 【探针】Win32 进程只有 2 GB 地址空间（CLAUDE.md 反复记过这个坑）。
// 报告可用虚拟地址空间：nc_open 抛 bad_alloc 时，要么是地址空间被吃穿，
// 要么是 HDF5 的大块 VirtualAlloc 失败后被 /EHa 翻成 std::bad_alloc。
static void ncdfLogAddrSpace(const char *tag) {
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);
	if (!GlobalMemoryStatusEx(&ms)) return;
	ncdfLog("[addr] %s: availVirtual=%.0fMB availPageFile=%.0fMB availPhys=%.0fMB\n",
		tag, ms.ullAvailVirtual / (1024.0 * 1024.0),
		ms.ullAvailPageFile / (1024.0 * 1024.0),
		ms.ullAvailPhys / (1024.0 * 1024.0));
}

// Lightweight metadata reader for tree building — no data allocation, no myDataVector modification.
// Returns date labels and data type availability for each timestep.
MainDialog::FileMetadata MainDialog::nc_read_metadata(const wxString &fileName) {
	FileMetadata meta;
	const wxCharBuffer mbBuf = fileName.mb_str();
	const char* filename = mbBuf.data();

	// 【探针】日志显示卡死点就落在本函数里（进来后既没走到 valid=true 也没走到
	// 返回空 meta）。逐段打点，下次一点按钮就能点名到具体哪一步。
	ncdfLog("[ncdfmeta] enter\n");
	ncdfLogAddrSpace("before nc_open");

	int ncid;
	int retval = nc_open(filename, NC_NOWRITE, &ncid);
	ncdfLog("[ncdfmeta] nc_open(NC_NOWRITE) returned %d\n", retval);
	if (retval != NC_NOERR) {
		ncdfLogAddrSpace("after failed open 1");
		retval = nc_open(filename, NC_NOWRITE | NC_NETCDF4, &ncid);
		ncdfLog("[ncdfmeta] nc_open(NC_NOWRITE|NC_NETCDF4) returned %d\n", retval);
	}
	ncdfLogAddrSpace("after nc_open");
	if (retval != NC_NOERR) { ncdfLog("[ncdfmeta] nc_open FAILED retval=%d\n", retval); return meta; }
	ncdfLog("[ncdfmeta] nc_open ok, ncid=%d\n", ncid);

	// Discover time variable
	int time_varid;
	{
		const char* alt_std[] = {NULL};
		const char* long_hints[] = {"Time", "time coordinate", NULL};
		const char* var_hints[] = {"time", "t", "Time", NULL};
		time_varid = find_var_by_cf(ncid, "time", alt_std, long_hints, var_hints);
	}
	ncdfLog("[ncdfmeta] time_varid=%d\n", time_varid);
	if (time_varid == -1) { nc_close(ncid); return meta; }

	int timeid = find_dim_by_var(ncid, time_varid, 0);
	ncdfLog("[ncdfmeta] time dimid=%d\n", timeid);
	if (timeid == -1) { nc_close(ncid); return meta; }

	size_t timelength = 0;
	if (nc_inq_dimlen(ncid, timeid, &timelength) != NC_NOERR || timelength == 0) {
		ncdfLog("[ncdfmeta] time dimlen FAILED or 0 (=%zu)\n", timelength);
		nc_close(ncid);
		return meta;
	}
	ncdfLog("[ncdfmeta] timelength=%zu\n", timelength);

	// Read time values
	double* time_out = (double*)calloc(timelength, sizeof(double));
	if (!time_out) { nc_close(ncid); return meta; }

	if (nc_get_var_double(ncid, time_varid, time_out) != NC_NOERR) {
		float* tf = (float*)calloc(timelength, sizeof(float));
		if (tf) {
			if (nc_get_var_float(ncid, time_varid, tf) == NC_NOERR)
				for (size_t i = 0; i < timelength; i++) time_out[i] = tf[i];
			free(tf);
		}
	}

	// Parse CF time units
	CFTimeInfo timeInfo;
	char* timeUnits = read_var_att_text(ncid, time_varid, "units");
	ncdfLog("[ncdfmeta] time values read, timeUnits=%s\n", timeUnits ? timeUnits : "(none)");
	if (timeUnits) {
		timeInfo = ParseCFTimeUnits(timeUnits);
		delete[] timeUnits;
	}
	ncdfLog("[ncdfmeta] CF units parsed: valid=%d secondsPerUnit=%g\n",
		(int)timeInfo.valid, timeInfo.secondsPerUnit);

	// Check data availability and cache variable IDs
	{
		const char* alt_std[] = {"surface_geostrophic_eastward_sea_water_velocity", "surface_eastward_sea_water_velocity", NULL};
		const char* long_hints[] = {"Eastward Current Velocity", NULL};
		const char* var_hints[] = {"u", "uo", "ugos", "current_u", NULL};
		meta.cached_u_varid = find_var_by_cf(ncid, "eastward_sea_water_velocity", alt_std, long_hints, var_hints);
		meta.hasCurrent = (meta.cached_u_varid != -1);
	}
	{
		const char* alt_std[] = {"surface_geostrophic_northward_sea_water_velocity", "surface_northward_sea_water_velocity", NULL};
		const char* long_hints[] = {"Northward Current Velocity", NULL};
		const char* var_hints[] = {"v", "vo", "vgos", "current_v", NULL};
		meta.cached_v_varid = find_var_by_cf(ncid, "northward_sea_water_velocity", alt_std, long_hints, var_hints);
	}
	{
		const char* alt_std[] = {"sea_surface_temperature", NULL};
		const char* long_hints[] = {"Sea surface temperature", NULL};
		const char* var_hints[] = {"thetao", "sst", "temperature", NULL};
		meta.cached_sst_varid = find_var_by_cf(ncid, "sea_water_temperature", alt_std, long_hints, var_hints);
		meta.hasSeaTemp = (meta.cached_sst_varid != -1);
	}
	{
		const char* alt_std[] = {"sea_surface_salinity", NULL};
		const char* long_hints[] = {"Sea surface salinity", NULL};
		const char* var_hints[] = {"so", "salinity", "salt", NULL};
		meta.cached_sal_varid = find_var_by_cf(ncid, "sea_water_salinity", alt_std, long_hints, var_hints);
		meta.hasSalinity = (meta.cached_sal_varid != -1);
	}
	{
		const char* alt_std[] = {"sea_surface_height", NULL};
		const char* long_hints[] = {"Sea surface height above geoid", "Sea surface height", NULL};
		const char* var_hints[] = {"zos", "ssh", "sea_surface_height", "eta", "SSH", NULL};
		meta.cached_zos_varid = find_var_by_cf(ncid, "sea_surface_height_above_geoid", alt_std, long_hints, var_hints);
		meta.hasZos = (meta.cached_zos_varid != -1);
	}
	ncdfLog("[ncdfmeta] var discovery done: u=%d v=%d sst=%d sal=%d zos=%d\n",
		meta.cached_u_varid, meta.cached_v_varid, meta.cached_sst_varid,
		meta.cached_sal_varid, meta.cached_zos_varid);

	// Read lat/lon coordinates and dimension sizes
	{
		int lat_varid, lon_varid;
		{
			const char* alt_std[] = {"projection_y_coordinate", NULL};
			const char* long_hints[] = {"Latitude", NULL};
			const char* var_hints[] = {"lat", "latitude", NULL};
			lat_varid = find_var_by_cf(ncid, "latitude", alt_std, long_hints, var_hints);
		}
		{
			const char* alt_std[] = {"projection_x_coordinate", NULL};
			const char* long_hints[] = {"Longitude", NULL};
			const char* var_hints[] = {"lon", "longitude", NULL};
			lon_varid = find_var_by_cf(ncid, "longitude", alt_std, long_hints, var_hints);
		}

		if (lat_varid != -1 && lon_varid != -1) {
			int latid = find_dim_by_var(ncid, lat_varid, 0);
			int lonid = find_dim_by_var(ncid, lon_varid, 0);
			if (latid != -1) nc_inq_dimlen(ncid, latid, &meta.latlength);
			if (lonid != -1) nc_inq_dimlen(ncid, lonid, &meta.lonlength);

			if (meta.latlength > 0) {
				double* lats = (double*)calloc(meta.latlength, sizeof(double));
				if (lats) {
					if (nc_get_var_double(ncid, lat_varid, lats) != NC_NOERR) {
						float* lf = (float*)calloc(meta.latlength, sizeof(float));
						if (lf) { nc_get_var_float(ncid, lat_varid, lf); for (size_t i = 0; i < meta.latlength; i++) lats[i] = lf[i]; free(lf); }
					}
					meta.firstGridPointLat = lats[0];
					meta.lastGridPointLat = lats[meta.latlength - 1];
					// Store in SharedCoords for later use
					meta.sharedCoords = std::make_shared<SharedCoords>();
					meta.sharedCoords->latValues = lats;
					meta.sharedCoords->latLength = meta.latlength;
				}
			}
			if (meta.lonlength > 0) {
				double* lons = (double*)calloc(meta.lonlength, sizeof(double));
				if (lons) {
					if (nc_get_var_double(ncid, lon_varid, lons) != NC_NOERR) {
						float* lf = (float*)calloc(meta.lonlength, sizeof(float));
						if (lf) { nc_get_var_float(ncid, lon_varid, lf); for (size_t i = 0; i < meta.lonlength; i++) lons[i] = lf[i]; free(lf); }
					}
					meta.firstGridPointLong = lons[0];
					meta.lastGridPointLong = lons[meta.lonlength - 1];
					if (meta.sharedCoords) {
						meta.sharedCoords->lonValues = lons;
						meta.sharedCoords->lonLength = meta.lonlength;
					}
				}
			}
			meta.iDirectionIncr = (meta.lonlength > 1) ?
				(meta.lastGridPointLong - meta.firstGridPointLong) / (meta.lonlength - 1) : 0;
			meta.jDirectionIncr = (meta.latlength > 1) ?
				(meta.lastGridPointLat - meta.firstGridPointLat) / (meta.latlength - 1) : 0;
		}
	}

	// Store time values for later use
	ncdfLog("[ncdfmeta] lat/lon done: latlen=%zu lonlen=%zu lat0=%g lon0=%g\n",
		meta.latlength, meta.lonlength, meta.firstGridPointLat, meta.firstGridPointLong);
	meta.timeValues.resize(timelength);
	for (size_t i = 0; i < timelength; i++) meta.timeValues[i] = time_out[i];

	// Build date list
	// 防御性上限：time 维要是被认错成别的维（例如数据维），下面三个 resize 会按
	// 上亿元素去分配，直接吃穿 Win32 的 2 GB 地址空间 -> bad_alloc -> 全局崩。
	// 元数据用的日期表超过 10 万条没有意义，宁可判为无效文件。
	if (timelength > 100000) {
		ncdfLog("[ncdfmeta] timelength=%zu implausible for metadata, treating file as invalid\n", timelength);
		free(time_out);
		nc_close(ncid);
		return meta;
	}
	ncdfLog("[ncdfmeta] building %zu date labels...\n", timelength);
	meta.timelength = timelength;
	meta.dates.resize(timelength);
	meta.timeValid.resize(timelength);
	for (size_t i = 0; i < timelength; i++) {
		if (timeInfo.valid) {
			// 时间值若是 _FillValue/NaN，sec 会是 inf/NaN，强转 (long long) 是未定义
			// 行为，wxTimeSpan 拿到天文数字后 wxDateTime::Add 会溢出 time_t。
			// 这里先判有限性，不合法就标无效日期，绝不走进未定义行为。
			double sec = time_out[i] * timeInfo.secondsPerUnit;
			if (!(sec == sec) || sec <= -1.0e15 || sec >= 1.0e15) {
				meta.dates[i] = wxDateTime();
				meta.timeValid[i] = false;
				continue;
			}
			wxTimeSpan span = wxTimeSpan::Seconds((long long)sec);
			meta.dates[i] = timeInfo.baseDateTime;
			meta.dates[i].Add(span);
			meta.timeValid[i] = true;
		} else {
			// Fallback: try to guess time format (same logic as nc_get)
			bool guessed = false;
			if (time_out[i] >= 1e9 && time_out[i] <= 2e9) {
				// Looks like Unix timestamp
				CFTimeInfo unixInfo = ParseCFTimeUnits("seconds since 1970-01-01");
				if (unixInfo.valid) {
					wxTimeSpan span = wxTimeSpan::Seconds((long long)(time_out[i] * unixInfo.secondsPerUnit));
					meta.dates[i] = unixInfo.baseDateTime;
					meta.dates[i].Add(span);
					guessed = true;
				}
			}
			if (!guessed && time_out[i] >= 0 && time_out[i] <= 700000) {
				// Looks like CMEMS hours since 1950-01-01
				CFTimeInfo cmemsInfo = ParseCFTimeUnits("hours since 1950-01-01");
				if (cmemsInfo.valid) {
					wxTimeSpan span = wxTimeSpan::Seconds((long long)(time_out[i] * cmemsInfo.secondsPerUnit));
					meta.dates[i] = cmemsInfo.baseDateTime;
					meta.dates[i].Add(span);
					guessed = true;
				}
			}
			meta.timeValid[i] = guessed;
			if (!guessed) meta.dates[i] = wxDateTime();
		}
	}

	ncdfLog("[ncdfmeta] date labels built, valid=1\n");
	meta.valid = true;
	free(time_out);
	nc_close(ncid);
	ncdfLog("[ncdfmeta] done\n");
	return meta;
}

int MainDialog::nc_get(wxString filestr){

	FILE *dbg = fopen("C:\\ProgramData\\opencpn\\ncdf_crash_trace.log", "a");
	if (dbg) { fprintf(dbg, "nc_get: ENTER\n"); fflush(dbg); fclose(dbg); }

	ncdfLog("[ncdf] nc_get: started\n");

	ncdfLog("[ncdf] nc_get: clearing myDataVector...\n");
	myDataVector.clear();
	ncdfLog("[ncdf] nc_get: myDataVector cleared\n");

	m_lastSelectedTimeIndex = -1;

	// Reset cached variable IDs (previous file may have had different variables)
	m_cached_u_varid = -1;
	m_cached_v_varid = -1;
	m_cached_sst_varid = -1;
	m_cached_sal_varid = -1;
	m_cached_zos_varid = -1;
	m_fileHasCurrent = false;
	m_fileHasSeaTemp = false;
	m_fileHasSalinity = false;
	m_fileHasZos = false;

	ncdfLog("[ncdf] nc_get: declaring variables...\n");
	int ncid;
	int time_varid, lat_varid, lon_varid;
	int u_varid, v_varid;
	int retval;

	int ndims_in, nvars_in, ngatts_in, unlimdimid_in;

	ncdfLog("[ncdf] nc_get: processing filename...\n");
	wxString filenameStr = filestr;
	ncdfLog("[ncdf] nc_get: filenameStr wxChar length=%d\n", (int)filenameStr.length());
	
	const wxCharBuffer mbBuf = filenameStr.mb_str();
	size_t mbLen = strlen(mbBuf);
	char* filename = new char[mbLen + 1];
	ncdfLog("[ncdf] nc_get: filename allocated, mbLen=%d\n", (int)mbLen);
	
	strcpy(filename, mbBuf);
	ncdfLog("[ncdf] nc_get: filename copied: %s\n", filename);

	/* Try opening without NC_NETCDF4 first (compatible with NetCDF3 classic format).
	   If that fails, retry with NC_NETCDF4 for HDF5-based NetCDF4 files. */
	retval = nc_open(filename, NC_NOWRITE, &ncid);
	if (retval != NC_NOERR) {
		retval = nc_open(filename, NC_NOWRITE | NC_NETCDF4, &ncid);
	}
	if (retval != NC_NOERR) {
		ncdfLog("[ncdf] nc_get: nc_open failed, retval=%d\n", retval);
		delete[] filename;
		return retval;
	}

	if ((retval = nc_inq(ncid, &ndims_in, &nvars_in, &ngatts_in, &unlimdimid_in))) {
		nc_close(ncid);
		delete[] filename;
		return retval;
	}

	if (ndims_in < 3 || ndims_in > 4) {
		ncdfLog("[ncdf] nc_get: unsupported ndims=%d (need 3-4)\n", ndims_in);
		nc_close(ncid);
		delete[] filename;
		return 2;
	}

	ncdfLog("[ncdf] nc_get: searching for variables using CF conventions...\n");

	/* u: eastward_sea_water_velocity */
	{
		const char* alt_std[] = {"surface_geostrophic_eastward_sea_water_velocity", "surface_eastward_sea_water_velocity", NULL};
		const char* long_hints[] = {"Eastward Current Velocity", "u-velocity component of current", NULL};
		const char* var_hints[] = {"u", "uo", "ugos", "current_u", "curr_u", "u10", "u100", NULL};
		u_varid = find_var_by_cf(ncid, "eastward_sea_water_velocity", alt_std, long_hints, var_hints);
	}
	
	/* v: northward_sea_water_velocity */
	{
		const char* alt_std[] = {"surface_geostrophic_northward_sea_water_velocity", "surface_northward_sea_water_velocity", NULL};
		const char* long_hints[] = {"Northward Current Velocity", "v-velocity component of current", NULL};
		const char* var_hints[] = {"v", "vo", "vgos", "current_v", "curr_v", "v10", "v100", NULL};
		v_varid = find_var_by_cf(ncid, "northward_sea_water_velocity", alt_std, long_hints, var_hints);
	}
	
	/* lat: latitude */
	{
		const char* alt_std[] = {"projection_y_coordinate", NULL};
		const char* long_hints[] = {"Latitude", "latitude coordinate", NULL};
		const char* var_hints[] = {"lat", "latitude", "nav_lat", NULL};
		lat_varid = find_var_by_cf(ncid, "latitude", alt_std, long_hints, var_hints);
	}
	
	/* lon: longitude */
	{
		const char* alt_std[] = {"projection_x_coordinate", NULL};
		const char* long_hints[] = {"Longitude", "longitude coordinate", NULL};
		const char* var_hints[] = {"lon", "longitude", "nav_lon", NULL};
		lon_varid = find_var_by_cf(ncid, "longitude", alt_std, long_hints, var_hints);
	}
	
	/* time */
	{
		const char* alt_std[] = {NULL};
		const char* long_hints[] = {"Time", "time coordinate", NULL};
		const char* var_hints[] = {"time", "t", "Time", NULL};
		time_varid = find_var_by_cf(ncid, "time", alt_std, long_hints, var_hints);
	}

	/* sea surface temperature (optional, just discover the variable) */
	int sst_varid = -1;
	{
		const char* alt_std[] = {"sea_surface_temperature", "surface_temperature", NULL};
		const char* long_hints[] = {"Sea surface temperature", "sea_water_temperature", "SST", "Temperature", "temperature", NULL};
		const char* var_hints[] = {"thetao", "sst", "temperature", "temp", "SST", "votemper", "to", NULL};
		sst_varid = find_var_by_cf(ncid, "sea_water_temperature", alt_std, long_hints, var_hints);
	}
	ncdfLog("[ncdf] nc_get: sst_varid=%d\n", sst_varid);

	/* sea surface salinity (optional) */
	int sal_varid = -1;
	{
		const char* alt_std[] = {"sea_surface_salinity", "surface_salinity", NULL};
		const char* long_hints[] = {"Sea surface salinity", "sea_water_salinity", "Salinity", "salinity", NULL};
		const char* var_hints[] = {"so", "salinity", "salt", "vosaline", "sos", "SSS", NULL};
		sal_varid = find_var_by_cf(ncid, "sea_water_salinity", alt_std, long_hints, var_hints);
	}
	ncdfLog("[ncdf] nc_get: sal_varid=%d\n", sal_varid);

	/* sea surface height above geoid (optional) */
	int zos_varid = -1;
	{
		const char* alt_std[] = {"sea_surface_height", NULL};
		const char* long_hints[] = {"Sea surface height above geoid", "Sea surface height", NULL};
		const char* var_hints[] = {"zos", "ssh", "sea_surface_height", "eta", "SSH", NULL};
		zos_varid = find_var_by_cf(ncid, "sea_surface_height_above_geoid", alt_std, long_hints, var_hints);
	}
	ncdfLog("[ncdf] nc_get: zos_varid=%d\n", zos_varid);

	/* depth - commented out */
	/*int depth_varid = -1;
	{
		const char* alt_std[] = {"depth", "z", "depth_below_sea_level", NULL};
		const char* long_hints[] = {"Depth", "depth coordinate", "vertical level", NULL};
		const char* var_hints[] = {"depth", "z", "Depth", "level", NULL};
		depth_varid = find_var_by_cf(ncid, "depth", alt_std, long_hints, var_hints);
	}*/

	ncdfLog("[ncdf] nc_get: result - u_varid=%d, v_varid=%d, lat_varid=%d, lon_varid=%d, time_varid=%d, sst_varid=%d, zos_varid=%d\n",
		u_varid, v_varid, lat_varid, lon_varid, time_varid, sst_varid, zos_varid);

	bool hasCurrent = (u_varid != -1 && v_varid != -1);
	bool hasSST = (sst_varid != -1);
	bool hasSalinity = (sal_varid != -1);
	bool hasZos = (zos_varid != -1);

	// Store file-level data availability and cached variable IDs
	m_fileHasCurrent = hasCurrent;
	m_fileHasSeaTemp = hasSST;
	m_fileHasSalinity = hasSalinity;
	m_fileHasZos = hasZos;
	m_cached_u_varid = u_varid;
	m_cached_v_varid = v_varid;
	m_cached_sst_varid = sst_varid;
	m_cached_sal_varid = sal_varid;
	m_cached_zos_varid = zos_varid;
	ncdfLog("[ncdf] nc_get: m_fileHasCurrent=%d, m_fileHasSeaTemp=%d, cached varids: u=%d v=%d sst=%d\n",
		(int)m_fileHasCurrent, (int)m_fileHasSeaTemp, m_cached_u_varid, m_cached_v_varid, m_cached_sst_varid);

	if (!hasCurrent && !hasSST) {
		ncdfLog("[ncdf] nc_get: no u/v and no SST, but loading file anyway for coordinate display\n");
		// Don't return error - allow the file to load for coordinate/time display
	}

	if (lat_varid == -1) {
		ncdfLog("[ncdf] nc_get: lat variable not found\n");
		nc_close(ncid);
		delete[] filename;
		return 2;
	}

	if (lon_varid == -1) {
		ncdfLog("[ncdf] nc_get: lon variable not found\n");
		nc_close(ncid);
		delete[] filename;
		return 2;
	}

	if (time_varid == -1) {
		ncdfLog("[ncdf] nc_get: time variable not found\n");
		nc_close(ncid);
		delete[] filename;
		return 2;
	}

	int timeid = find_dim_by_var(ncid, time_varid, 0);
	int latid = find_dim_by_var(ncid, lat_varid, 0);
	int lonid = find_dim_by_var(ncid, lon_varid, 0);
	/* depth - commented out */
	/*int depthid = -1;
	if (depth_varid != -1) {
		depthid = find_dim_by_var(ncid, depth_varid, 0);
	}*/

	if (timeid == -1 || latid == -1 || lonid == -1) {
		ncdfLog("[ncdf] nc_get: invalid dimension id (timeid=%d, latid=%d, lonid=%d)\n", timeid, latid, lonid);
		nc_close(ncid);
		delete[] filename;
		return 2;
	}

	size_t timelength = 0, latlength = 0, lonlength = 0/*, depthlength = 0*/;
	int status;

	if ((status = nc_inq_dimlen(ncid, timeid, &timelength)) != NC_NOERR) {
		ncdfLog("[ncdf] nc_get: failed to get time dimension length\n");
		nc_close(ncid);
		delete[] filename;
		return 2;
	}
	if ((status = nc_inq_dimlen(ncid, latid, &latlength)) != NC_NOERR) {
		ncdfLog("[ncdf] nc_get: failed to get lat dimension length\n");
		nc_close(ncid);
		delete[] filename;
		return 2;
	}
	if ((status = nc_inq_dimlen(ncid, lonid, &lonlength)) != NC_NOERR) {
		ncdfLog("[ncdf] nc_get: failed to get lon dimension length\n");
		nc_close(ncid);
		delete[] filename;
		return 2;
	}

	ncdfLog("[ncdf] nc_get: timelength=%d, latlength=%d, lonlength=%d\n", 
		(int)timelength, (int)latlength, (int)lonlength);

	wxDouble* lats = (wxDouble*)calloc(latlength, sizeof(wxDouble));
	wxDouble* lons = (wxDouble*)calloc(lonlength, sizeof(wxDouble));
	double* time_out = (double*)calloc(timelength, sizeof(double));

	if (!lats || !lons || !time_out) {
		if (lats) free(lats);
		if (lons) free(lons);
		if (time_out) free(time_out);
		nc_close(ncid);
		delete[] filename;
		return 1;
	}

	if (nc_get_var_double(ncid, lat_varid, (double*)lats) != NC_NOERR) {
		float* lat_float = (float*)calloc(latlength, sizeof(float));
		if (!lat_float) { free(lats); free(lons); free(time_out); nc_close(ncid); delete[] filename; return 1; }
		if (nc_get_var_float(ncid, lat_varid, lat_float) == NC_NOERR) {
			for (size_t i = 0; i < latlength; i++) {
				lats[i] = lat_float[i];
			}
		}
		free(lat_float);
	}
	
	if (nc_get_var_double(ncid, lon_varid, (double*)lons) != NC_NOERR) {
		float* lon_float = (float*)calloc(lonlength, sizeof(float));
		if (!lon_float) { free(lons); free(lats); free(time_out); nc_close(ncid); delete[] filename; return 1; }
		if (nc_get_var_float(ncid, lon_varid, lon_float) == NC_NOERR) {
			for (size_t i = 0; i < lonlength; i++) {
				lons[i] = lon_float[i];
			}
		}
		free(lon_float);
	}
	
	if (nc_get_var_double(ncid, time_varid, time_out) != NC_NOERR) {
		float* time_float = (float*)calloc(timelength, sizeof(float));
		if (!time_float) { free(time_out); free(lats); free(lons); nc_close(ncid); delete[] filename; return 1; }
		if (nc_get_var_float(ncid, time_varid, time_float) == NC_NOERR) {
			for (size_t i = 0; i < timelength; i++) {
				time_out[i] = time_float[i];
			}
		}
		free(time_float);
	}
	
	ncdfLog("[ncdf] nc_get: first time value = %f\n", timelength > 0 ? time_out[0] : 0.0);
	
	/* depth - commented out */
	/*wxDouble* depths = NULL;
	if (depth_varid != -1 && depthid != -1) {
		if ((status = nc_inq_dimlen(ncid, depthid, &depthlength)) != NC_NOERR) {
			ncdfLog("[ncdf] nc_get: failed to get depth dimension length, assuming surface data\n");
			depthlength = 0;
		} else {
			if (depthlength > 0) {
				depths = (wxDouble*)calloc(depthlength, sizeof(wxDouble));
				if (depths) {
					if (nc_get_var_double(ncid, depth_varid, (double*)depths) != NC_NOERR) {
						float* depth_float = (float*)calloc(depthlength, sizeof(float));
						if (depth_float) {
							if (nc_get_var_float(ncid, depth_varid, depth_float) == NC_NOERR) {
								for (size_t i = 0; i < depthlength; i++) {
									depths[i] = depth_float[i];
								}
							}
							free(depth_float);
						}
					}
					ncdfLog("[ncdf] nc_get: depth length=%d, first depth value=%f\n", 
						(int)depthlength, depths[0]);
				}
			} else {
				ncdfLog("[ncdf] nc_get: depth dimension length is 0, assuming surface data\n");
				depthlength = 0;
			}
		}
	} else {
		depthlength = 0;
	}*/

	float firstGridPointLat;
	float firstGridPointLon;
	float lastGridPointLat;
	float lastGridPointLon;
	float scale_factor = 1.0f;

	// Always use actual coordinate arrays for bounds (GRIB pattern)
	// Global attributes (latitude_min etc.) may not match the data grid exactly
	firstGridPointLat = lats[0];
	firstGridPointLon = lons[0];
	lastGridPointLat = lats[latlength - 1];
	lastGridPointLon = lons[lonlength - 1];
	
	if (nc_get_att_float(ncid, u_varid, "scale_factor", &scale_factor) != NC_NOERR) {
		scale_factor = 1.0f;
	}
	
	/* Read CF time units and parse epoch + unit */
	CFTimeInfo timeInfo;
	{
		char* timeUnits = read_var_att_text(ncid, time_varid, "units");
		if (!timeUnits) {
			timeUnits = read_var_att_text(ncid, NC_GLOBAL, "units");
			if (timeUnits) ncdfLog("[ncdf] nc_get: found time units from NC_GLOBAL: '%s'\n", timeUnits);
		}
		
		if (!timeUnits && timelength > 0) {
			double firstTimeValue = time_out[0];
			double timeStep = timelength > 1 ? time_out[1] - time_out[0] : 1.0;
			
			bool canDetermine = false;
			const int MAX_YEAR = 2050;
			
			if (firstTimeValue >= 0 && firstTimeValue <= 2000000000.0) {
				if (timeStep >= 3600.0 || timeStep >= 86400.0) {
					CFTimeInfo unixInfo = ParseCFTimeUnits("seconds since 1970-01-01");
					if (unixInfo.valid) {
						wxDateTime testDate = unixInfo.baseDateTime;
						testDate.Add(wxTimeSpan::Seconds((long long)(firstTimeValue * unixInfo.secondsPerUnit)));
						if (testDate.GetYear() <= MAX_YEAR) {
							ncdfLog("[ncdf] nc_get: WARNING - no units, guessed Unix timestamp, first date=%s (valid, <=%d)\n", 
								(const char*)testDate.Format("%Y-%m-%d").mb_str(), MAX_YEAR);
							timeInfo = unixInfo;
							canDetermine = true;
						} else {
							ncdfLog("[ncdf] nc_get: WARNING - no units, guessed Unix timestamp but first date=%s (>%d), rejecting\n", 
								(const char*)testDate.Format("%Y-%m-%d").mb_str(), MAX_YEAR);
						}
					}
				}
			}
			
			if (!canDetermine && firstTimeValue >= 0 && firstTimeValue <= 700000.0) {
				if (timeStep >= 1.0 && timeStep <= 24.0) {
					CFTimeInfo cmemsInfo = ParseCFTimeUnits("hours since 1950-01-01");
					if (cmemsInfo.valid) {
						wxDateTime testDate = cmemsInfo.baseDateTime;
						testDate.Add(wxTimeSpan::Seconds((long long)(firstTimeValue * cmemsInfo.secondsPerUnit)));
						if (testDate.GetYear() <= MAX_YEAR) {
							ncdfLog("[ncdf] nc_get: WARNING - no units, guessed CMEMS hours, first date=%s (valid, <=%d)\n", 
								(const char*)testDate.Format("%Y-%m-%d").mb_str(), MAX_YEAR);
							timeInfo = cmemsInfo;
							canDetermine = true;
						} else {
							ncdfLog("[ncdf] nc_get: WARNING - no units, guessed CMEMS hours but first date=%s (>%d), rejecting\n", 
								(const char*)testDate.Format("%Y-%m-%d").mb_str(), MAX_YEAR);
						}
					}
				}
			}
			
			if (!canDetermine) {
				ncdfLog("[ncdf] nc_get: WARNING - no units, time value=%.0f, step=%.0f, cannot reliably determine time unit, using sequential labels\n", firstTimeValue, timeStep);
				timeInfo.valid = false;
			}
		} else if (!timeUnits) {
			ncdfLog("[ncdf] nc_get: WARNING - no units and no time data, using sequential labels\n");
			timeInfo.valid = false;
		} else {
			ncdfLog("[ncdf] nc_get: time units = '%s'\n", timeUnits);
			timeInfo = ParseCFTimeUnits(timeUnits);
			delete[] timeUnits;
		}
		if (timeInfo.valid) {
			wxString fmt = timeInfo.baseDateTime.Format("%Y-%m-%d %H:%M:%S");
			ncdfLog("[ncdf] nc_get: time epoch = %s, secondsPerUnit=%f\n",
				(const char*)fmt.mb_str(), timeInfo.secondsPerUnit);
		} else {
			ncdfLog("[ncdf] nc_get: WARNING - failed to parse time units, using fallback\n");
		}
	}
	
	//
	wxDouble iDirectionIncr = (lastGridPointLon - firstGridPointLon) / (lonlength > 1 ? lonlength - 1 : 1);
	wxDouble jDirectionIncr = (lastGridPointLat - firstGridPointLat) / (latlength > 1 ? latlength - 1 : 1);

	// Item 1: Allocate shared coordinate arrays once (all time steps share the same data)
	std::shared_ptr<SharedCoords> sharedCoords = std::make_shared<SharedCoords>();
	sharedCoords->latValues = (wxDouble*)calloc(latlength, sizeof(wxDouble));
	sharedCoords->lonValues = (wxDouble*)calloc(lonlength, sizeof(wxDouble));
	sharedCoords->timeValues = (double*)calloc(timelength, sizeof(double));
	sharedCoords->latLength = latlength;

	// Pre-allocate file-level data buffers (reused across time steps, avoids 32-bit heap fragmentation)
	size_t nbr_uv = latlength * lonlength;
	m_fileUBuffer.reserve(nbr_uv);
	m_fileVBuffer.reserve(nbr_uv);
	m_fileSSTBuffer.reserve(nbr_uv);
	m_fileSalBuffer.reserve(nbr_uv);
	m_fileBufferReady = true;
	ncdfLog("[ncdf] nc_get: file buffers reserved, nbr_uv=%zu\n", nbr_uv);
	sharedCoords->lonLength = lonlength;
	sharedCoords->timeLength = timelength;
	memcpy(sharedCoords->latValues, lats, latlength * sizeof(wxDouble));
	memcpy(sharedCoords->lonValues, lons, lonlength * sizeof(wxDouble));
	memcpy(sharedCoords->timeValues, time_out, timelength * sizeof(double));

	// Create message for each timestep (metadata only, shared coords)
	for (size_t i = 0; i < timelength; i++) {

		myncdfData.clear();
		myncdfData.timeValid = timeInfo.valid;

		if (timeInfo.valid) {
			wxTimeSpan span = wxTimeSpan::Seconds((long long)(time_out[i] * timeInfo.secondsPerUnit));
			myncdfData.dataDateTime = timeInfo.baseDateTime;
			myncdfData.dataDateTime.Add(span);
			myncdfData.minutesAfterStart = (int)(time_out[i] * timeInfo.secondsPerUnit / 60.0);

			wxString dateStr = myncdfData.dataDateTime.Format("%Y-%m-%d %H:%M:%S");
			ncdfLog("[ncdf] nc_get: timestep=%d, raw_time=%f, calculated_date=%s\n",
				(int)i, time_out[i], (const char*)dateStr.mb_str());
		} else {
			myncdfData.dataDateTime = wxDateTime();
			myncdfData.minutesAfterStart = (int)(i * 60);
			ncdfLog("[ncdf] nc_get: timestep=%d, raw_time=%f, timeValid=false, using sequential label, minutesAfterStart=%d\n",
				(int)i, time_out[i], myncdfData.minutesAfterStart);
		}
		myncdfData.numberOfPoints = (int)(latlength * lonlength);
		myncdfData.noPointsParallel = (wxUint32)lonlength;
		myncdfData.noPointsMeridian = (wxUint32)latlength;
		myncdfData.firstGridPointLat = firstGridPointLat;
		myncdfData.firstGridPointLong = firstGridPointLon;
		myncdfData.lastGridPointLat = lastGridPointLat;
		myncdfData.lastGridPointLong = lastGridPointLon;
		if (i == 0) {
			ncdfLog("[ncdf] nc_get: bounds from file: lat[%.2f,%.2f] lon[%.2f,%.2f] file=%s\n",
				firstGridPointLat, lastGridPointLat, firstGridPointLon, lastGridPointLon, filename);
		}
		myncdfData.iDirectionIncr = iDirectionIncr;
		myncdfData.jDirectionIncr = jDirectionIncr;
		myncdfData.fileName = filename;
		myncdfData.timeIndex = (int)i;
		myncdfData.timeLength = timelength;
		myncdfData.latLength = latlength;
		myncdfData.lonLength = lonlength;
		myncdfData.hasSeaTemp = hasSST;
		myncdfData.hasSalinity = hasSalinity;
		myncdfData.hasZos = hasZos;
		/*myncdfData.depthLength = depthlength;*/

		// Share coordinate arrays across all time steps (one allocation)
		myncdfData.sharedCoords = sharedCoords;
		myncdfData.latValues = sharedCoords->latValues;
		myncdfData.lonValues = sharedCoords->lonValues;
		myncdfData.timeValues = sharedCoords->timeValues;

		myDataVector.push_back(myncdfData);
	}
	
	nc_close(ncid);
	
	free(time_out);
	free(lats);
	free(lons);
	/*if (depths) {
		free(depths);
	}*/
	
	delete[] filename;


	return 0;
}

static bool readUVFromNC(int ncid, int u_varid, int v_varid, ncdfDataMessage& dataMessage) {
	ncdfLog("[ncdf] readUVFromNC: u_varid=%d, v_varid=%d\n", u_varid, v_varid);

	float fill_value_u = -32767.0f;
	float fill_value_v = -32767.0f;

	if (nc_get_att_float(ncid, u_varid, "_FillValue", &fill_value_u) != NC_NOERR) {
		double fill_val_double;
		if (nc_get_att_double(ncid, u_varid, "_FillValue", &fill_val_double) == NC_NOERR) {
			fill_value_u = (float)fill_val_double;
		} else {
			fill_value_u = -32767.0f;
		}
	}

	if (nc_get_att_float(ncid, v_varid, "_FillValue", &fill_value_v) != NC_NOERR) {
		double fill_val_double;
		if (nc_get_att_double(ncid, v_varid, "_FillValue", &fill_val_double) == NC_NOERR) {
			fill_value_v = (float)fill_val_double;
		} else {
			fill_value_v = -32767.0f;
		}
	}

	// 【scale_factor / add_offset 必须自己应用】netCDF C API 只做数值类型转换，
	// **不会**按 CF 约定解包。AVISO L4 的 ugos/vgos 是 int32 + scale_factor=0.0001，
	// 不解包就会把 -9830000 当成 -9830000 m/s 用（2026-10-29 实测 uMax=1316 m/s）。
	double scale_u = 1.0, offset_u = 0.0;
	double scale_v = 1.0, offset_v = 0.0;
	if (nc_get_att_double(ncid, u_varid, "scale_factor", &scale_u) != NC_NOERR) scale_u = 1.0;
	if (nc_get_att_double(ncid, u_varid, "add_offset",   &offset_u) != NC_NOERR) offset_u = 0.0;
	if (nc_get_att_double(ncid, v_varid, "scale_factor", &scale_v) != NC_NOERR) scale_v = 1.0;
	if (nc_get_att_double(ncid, v_varid, "add_offset",   &offset_v) != NC_NOERR) offset_v = 0.0;
	ncdfLog("[ncdf] readUVFromNC: scale_u=%.8g offset_u=%.8g scale_v=%.8g offset_v=%.8g\n",
	        scale_u, offset_u, scale_v, offset_v);

	VarDimInfo dims = discover_var_dims(ncid, u_varid);
	if (!dims.valid) return false;

	int ndims;
	nc_inq_varndims(ncid, u_varid, &ndims);

	size_t start[4] = {0, 0, 0, 0};
	size_t count[4] = {1, 1, 1, 1};

	start[dims.time_idx] = dataMessage.timeIndex;
	count[dims.time_idx] = 1;
	start[dims.lat_idx] = 0;
	count[dims.lat_idx] = dataMessage.latLength;
	start[dims.lon_idx] = 0;
	count[dims.lon_idx] = dataMessage.lonLength;
	if (dims.depth_idx != -1) {
		start[dims.depth_idx] = 0;
		count[dims.depth_idx] = 1;
	}

	size_t nbr_uv = dataMessage.latLength * dataMessage.lonLength;

	float* u_vals = (float*)calloc(nbr_uv, sizeof(float));
	if (!u_vals) return false;

	float* v_vals = (float*)calloc(nbr_uv, sizeof(float));
	if (!v_vals) { free(u_vals); return false; }

	nc_type u_type, v_type;
	nc_inq_vartype(ncid, u_varid, &u_type);
	nc_inq_vartype(ncid, v_varid, &v_type);

	ncdfLog("[ncdf] readUVFromNC: reading data, u_type=%d, v_type=%d, nbr_uv=%zu\n",
		(int)u_type, (int)v_type, nbr_uv);

	int retval;
	if (u_type == NC_DOUBLE) {
		double* u_vals_double = (double*)calloc(nbr_uv, sizeof(double));
		if (!u_vals_double) { free(u_vals); free(v_vals); return false; }
		if ((retval = nc_get_vara_double(ncid, u_varid, start, count, u_vals_double))) {
			free(u_vals_double); free(u_vals); free(v_vals); return false;
		}
		for (size_t i = 0; i < nbr_uv; i++) {
			u_vals[i] = (float)u_vals_double[i];
		}
		free(u_vals_double);
	} else {
		if ((retval = nc_get_vara_float(ncid, u_varid, start, count, u_vals))) {
			free(u_vals); free(v_vals); return false;
		}
	}

	if (v_type == NC_DOUBLE) {
		double* v_vals_double = (double*)calloc(nbr_uv, sizeof(double));
		if (!v_vals_double) { free(u_vals); free(v_vals); return false; }
		if ((retval = nc_get_vara_double(ncid, v_varid, start, count, v_vals_double))) {
			free(v_vals_double); free(u_vals); free(v_vals); return false;
		}
		for (size_t i = 0; i < nbr_uv; i++) {
			v_vals[i] = (float)v_vals_double[i];
		}
		free(v_vals_double);
	} else {
		if ((retval = nc_get_vara_float(ncid, v_varid, start, count, v_vals))) {
			free(u_vals); free(v_vals); return false;
		}
	}

	ncdfLog("[ncdf] readUVFromNC: data read successfully, allocating output arrays\n");

	// assign() 而非 resize()：同尺寸时 resize 是空操作，会让上一时间步的旧值
	// 在缺测格点上残留（时间步切换的隐性脏数据）。
	dataMessage.ucurr.assign(nbr_uv, ncdf_NOTDEF);
	dataMessage.vcurr.assign(nbr_uv, ncdf_NOTDEF);

	// 缺测判定：_FillValue 可能是 NaN（CMEMS GLO12 即如此）。旧写法 `x != fill`
	// 在 fill=NaN 时恒为真，陆点会以 NaN 写入并破坏 getInterpolatedUV 的角点
	// 有效性统计（近岸双线性插值失效）。
	// 规则：数据为 NaN 一律缺测；fill 非 NaN 时再比对相等。
	auto isMissing = [](float val, float fill) {
		if (!(val == val)) return true;
		if (fill == fill && val == fill) return true;
		return false;
	};

	size_t count_records = 0;
	if (dims.lat_before_lon) {
		for (size_t j = 0; j < dataMessage.latLength; j++) {
			for (size_t k = 0; k < dataMessage.lonLength; k++) {
				if (!isMissing(u_vals[count_records], fill_value_u) &&
				    !isMissing(v_vals[count_records], fill_value_v)) {
					dataMessage.ucurr[count_records] = (double)u_vals[count_records] * scale_u + offset_u;
					dataMessage.vcurr[count_records] = (double)v_vals[count_records] * scale_v + offset_v;
				}
				count_records++;
			}
		}
	} else {
		for (size_t k = 0; k < dataMessage.lonLength; k++) {
			for (size_t j = 0; j < dataMessage.latLength; j++) {
				if (!isMissing(u_vals[count_records], fill_value_u) &&
				    !isMissing(v_vals[count_records], fill_value_v)) {
					dataMessage.ucurr[count_records] = (double)u_vals[count_records] * scale_u + offset_u;
					dataMessage.vcurr[count_records] = (double)v_vals[count_records] * scale_v + offset_v;
				}
				count_records++;
			}
		}
	}

	free(u_vals);
	free(v_vals);
	return true;
}

static bool readSSTFromNC(int ncid, int sst_varid, ncdfDataMessage& dataMessage) {
	ncdfLog("[ncdf] readSSTFromNC: sst_varid=%d\n", sst_varid);

	if (sst_varid < 0) {
		// Fallback: discover SST variable if cache miss
		const char* alt_std[] = {"sea_surface_temperature", "surface_temperature", NULL};
		const char* long_hints[] = {"Sea surface temperature", "sea_water_temperature", "SST", "Temperature", "temperature", NULL};
		const char* var_hints[] = {"thetao", "sst", "temperature", "temp", "SST", "votemper", "to", NULL};
		sst_varid = find_var_by_cf(ncid, "sea_water_temperature", alt_std, long_hints, var_hints);
	}
	if (sst_varid == -1) return false;

	VarDimInfo dims = discover_var_dims(ncid, sst_varid);
	if (!dims.valid) return false;

	int ndims;
	nc_inq_varndims(ncid, sst_varid, &ndims);

	size_t start[NC_MAX_DIMS] = {0};
	size_t count[NC_MAX_DIMS] = {0};

	start[dims.time_idx] = dataMessage.timeIndex;
	count[dims.time_idx] = 1;
	start[dims.lat_idx] = 0;
	count[dims.lat_idx] = dataMessage.latLength;
	start[dims.lon_idx] = 0;
	count[dims.lon_idx] = dataMessage.lonLength;
	if (dims.depth_idx != -1) {
		start[dims.depth_idx] = 0;
		count[dims.depth_idx] = 1;
	}

	size_t nbr_uv = dataMessage.latLength * dataMessage.lonLength;

	nc_type var_type;
	nc_inq_vartype(ncid, sst_varid, &var_type);

	float* sst_vals = (float*)calloc(nbr_uv, sizeof(float));
	if (!sst_vals) return false;

	if (var_type == NC_DOUBLE) {
		double* sst_double = (double*)calloc(nbr_uv, sizeof(double));
		if (!sst_double) { free(sst_vals); return false; }
		if (nc_get_vara_double(ncid, sst_varid, start, count, sst_double) != NC_NOERR) {
			free(sst_double); free(sst_vals); return false;
		}
		for (size_t i = 0; i < nbr_uv; i++) sst_vals[i] = (float)sst_double[i];
		free(sst_double);
	} else {
		if (nc_get_vara_float(ncid, sst_varid, start, count, sst_vals) != NC_NOERR) {
			free(sst_vals); return false;
		}
	}

	bool isKelvin = false;
	char units_str[64] = {0};
	if (nc_get_att_text(ncid, sst_varid, "units", units_str) == NC_NOERR) {
		if (strstr(units_str, "K") || strstr(units_str, "kelvin")) isKelvin = true;
	}

	float sst_fill = -32767.0f;
	nc_get_att_float(ncid, sst_varid, "_FillValue", &sst_fill);

	dataMessage.sst.resize(nbr_uv, ncdf_NOTDEF);

	// Rearrange data to [lat, lon] order regardless of file dimension order
	size_t latLen = dataMessage.latLength;
	size_t lonLen = dataMessage.lonLength;
	size_t count_records = 0;
	double minValid = isKelvin ? 100.0 : -10.0;
	double maxValid = isKelvin ? 370.0 : 50.0;

	if (dims.lat_before_lon) {
		for (size_t j = 0; j < latLen; j++) {
			for (size_t k = 0; k < lonLen; k++) {
				double val = (double)sst_vals[count_records];
				if (!isnan(val) && isfinite(val) && val != sst_fill && val > minValid && val < maxValid)
					dataMessage.sst[count_records] = isKelvin ? (val - 273.15) : val;
				else
					dataMessage.sst[count_records] = ncdf_NOTDEF;
				count_records++;
			}
		}
	} else {
		// File order is [lon, lat] — reorder to [lat, lon]
		float* reordered = (float*)calloc(nbr_uv, sizeof(float));
		if (reordered) {
			for (size_t k = 0; k < lonLen; k++)
				for (size_t j = 0; j < latLen; j++)
					reordered[j * lonLen + k] = sst_vals[k * latLen + j];
			for (size_t i = 0; i < nbr_uv; i++) {
				double val = (double)reordered[i];
				if (!isnan(val) && isfinite(val) && val != sst_fill && val > minValid && val < maxValid)
					dataMessage.sst[i] = isKelvin ? (val - 273.15) : val;
				else
					dataMessage.sst[i] = ncdf_NOTDEF;
			}
			free(reordered);
		}
	}

	free(sst_vals);
	ncdfLog("[ncdf] readSSTFromNC: SST read, isKelvin=%d, lat_before_lon=%d\n",
		(int)isKelvin, (int)dims.lat_before_lon);
	dataMessage.hasSeaTemp = true;
	return true;
}

static bool readSalinityFromNC(int ncid, int sal_varid, ncdfDataMessage& dataMessage) {
	ncdfLog("[ncdf] readSalinityFromNC: sal_varid=%d\n", sal_varid);

	if (sal_varid < 0) {
		const char* alt_std[] = {"sea_surface_salinity", "surface_salinity", NULL};
		const char* long_hints[] = {"Sea surface salinity", "sea_water_salinity", "Salinity", "salinity", NULL};
		const char* var_hints[] = {"so", "salinity", "salt", "vosaline", "sos", "SSS", NULL};
		sal_varid = find_var_by_cf(ncid, "sea_water_salinity", alt_std, long_hints, var_hints);
	}
	if (sal_varid == -1) return false;

	VarDimInfo dims = discover_var_dims(ncid, sal_varid);
	if (!dims.valid) return false;

	int ndims;
	nc_inq_varndims(ncid, sal_varid, &ndims);

	size_t start[NC_MAX_DIMS] = {0};
	size_t count[NC_MAX_DIMS] = {0};

	start[dims.time_idx] = dataMessage.timeIndex;
	count[dims.time_idx] = 1;
	start[dims.lat_idx] = 0;
	count[dims.lat_idx] = dataMessage.latLength;
	start[dims.lon_idx] = 0;
	count[dims.lon_idx] = dataMessage.lonLength;
	if (dims.depth_idx != -1) {
		start[dims.depth_idx] = 0;
		count[dims.depth_idx] = 1;
	}

	size_t nbr_uv = dataMessage.latLength * dataMessage.lonLength;

	nc_type var_type;
	nc_inq_vartype(ncid, sal_varid, &var_type);

	float* sal_vals = (float*)calloc(nbr_uv, sizeof(float));
	if (!sal_vals) return false;

	if (var_type == NC_DOUBLE) {
		double* sal_double = (double*)calloc(nbr_uv, sizeof(double));
		if (!sal_double) { free(sal_vals); return false; }
		if (nc_get_vara_double(ncid, sal_varid, start, count, sal_double) != NC_NOERR) {
			free(sal_double); free(sal_vals); return false;
		}
		for (size_t i = 0; i < nbr_uv; i++) sal_vals[i] = (float)sal_double[i];
		free(sal_double);
	} else {
		if (nc_get_vara_float(ncid, sal_varid, start, count, sal_vals) != NC_NOERR) {
			free(sal_vals); return false;
		}
	}

	float sal_fill = -32767.0f;
	nc_get_att_float(ncid, sal_varid, "_FillValue", &sal_fill);

	dataMessage.salinity.resize(nbr_uv, ncdf_NOTDEF);

	// Rearrange data to [lat, lon] order regardless of file dimension order
	size_t latLen = dataMessage.latLength;
	size_t lonLen = dataMessage.lonLength;
	size_t count_records = 0;

	if (dims.lat_before_lon) {
		for (size_t j = 0; j < latLen; j++) {
			for (size_t k = 0; k < lonLen; k++) {
				double val = (double)sal_vals[count_records];
				if (!isnan(val) && isfinite(val) && val != sal_fill && val > 0.0 && val < 100.0)
					dataMessage.salinity[count_records] = val;
				else
					dataMessage.salinity[count_records] = ncdf_NOTDEF;
				count_records++;
			}
		}
	} else {
		// File order is [lon, lat] — reorder to [lat, lon]
		float* reordered = (float*)calloc(nbr_uv, sizeof(float));
		if (reordered) {
			for (size_t k = 0; k < lonLen; k++)
				for (size_t j = 0; j < latLen; j++)
					reordered[j * lonLen + k] = sal_vals[k * latLen + j];
			for (size_t i = 0; i < nbr_uv; i++) {
				double val = (double)reordered[i];
				if (!isnan(val) && isfinite(val) && val != sal_fill && val > 0.0 && val < 100.0)
					dataMessage.salinity[i] = val;
				else
					dataMessage.salinity[i] = ncdf_NOTDEF;
			}
			free(reordered);
		}
	}

	free(sal_vals);
	ncdfLog("[ncdf] readSalinityFromNC: Salinity read, lat_before_lon=%d\n", (int)dims.lat_before_lon);
	dataMessage.hasSalinity = true;
	return true;
}

static bool readZosFromNC(int ncid, int zos_varid, ncdfDataMessage& dataMessage) {
	ncdfLog("[ncdf] readZosFromNC: zos_varid=%d\n", zos_varid);

	if (zos_varid < 0) {
		const char* alt_std[] = {"sea_surface_height", NULL};
		const char* long_hints[] = {"Sea surface height above geoid", "Sea surface height", NULL};
		const char* var_hints[] = {"zos", "ssh", "sea_surface_height", "eta", "SSH", NULL};
		zos_varid = find_var_by_cf(ncid, "sea_surface_height_above_geoid", alt_std, long_hints, var_hints);
	}
	if (zos_varid == -1) return false;

	VarDimInfo dims = discover_var_dims(ncid, zos_varid);
	if (!dims.valid) return false;

	int ndims;
	nc_inq_varndims(ncid, zos_varid, &ndims);

	size_t start[NC_MAX_DIMS] = {0};
	size_t count[NC_MAX_DIMS] = {0};

	start[dims.time_idx] = dataMessage.timeIndex;
	count[dims.time_idx] = 1;
	start[dims.lat_idx] = 0;
	count[dims.lat_idx] = dataMessage.latLength;
	start[dims.lon_idx] = 0;
	count[dims.lon_idx] = dataMessage.lonLength;
	if (dims.depth_idx != -1) {
		start[dims.depth_idx] = 0;
		count[dims.depth_idx] = 1;
	}

	size_t nbr_uv = dataMessage.latLength * dataMessage.lonLength;

	nc_type var_type;
	nc_inq_vartype(ncid, zos_varid, &var_type);

	float* zos_vals = (float*)calloc(nbr_uv, sizeof(float));
	if (!zos_vals) return false;

	if (var_type == NC_DOUBLE) {
		double* zos_double = (double*)calloc(nbr_uv, sizeof(double));
		if (!zos_double) { free(zos_vals); return false; }
		if (nc_get_vara_double(ncid, zos_varid, start, count, zos_double) != NC_NOERR) {
			free(zos_double); free(zos_vals); return false;
		}
		for (size_t i = 0; i < nbr_uv; i++) zos_vals[i] = (float)zos_double[i];
		free(zos_double);
	} else {
		if (nc_get_vara_float(ncid, zos_varid, start, count, zos_vals) != NC_NOERR) {
			free(zos_vals); return false;
		}
	}

	float zos_fill = -32767.0f;
	nc_get_att_float(ncid, zos_varid, "_FillValue", &zos_fill);

	dataMessage.zos.resize(nbr_uv, ncdf_NOTDEF);

	size_t latLen = dataMessage.latLength;
	size_t lonLen = dataMessage.lonLength;
	size_t count_records = 0;

	if (dims.lat_before_lon) {
		for (size_t j = 0; j < latLen; j++) {
			for (size_t k = 0; k < lonLen; k++) {
				double val = (double)zos_vals[count_records];
				if (!isnan(val) && isfinite(val) && val != zos_fill && val > -10.0 && val < 10.0)
					dataMessage.zos[count_records] = val;
				else
					dataMessage.zos[count_records] = ncdf_NOTDEF;
				count_records++;
			}
		}
	} else {
		float* reordered = (float*)calloc(nbr_uv, sizeof(float));
		if (reordered) {
			for (size_t k = 0; k < lonLen; k++)
				for (size_t j = 0; j < latLen; j++)
					reordered[j * lonLen + k] = zos_vals[k * latLen + j];
			for (size_t i = 0; i < nbr_uv; i++) {
				double val = (double)reordered[i];
				if (!isnan(val) && isfinite(val) && val != zos_fill && val > -10.0 && val < 10.0)
					dataMessage.zos[i] = val;
				else
					dataMessage.zos[i] = ncdf_NOTDEF;
			}
			free(reordered);
		}
	}

	free(zos_vals);
	ncdfLog("[ncdf] readZosFromNC: Zos read, lat_before_lon=%d\n", (int)dims.lat_before_lon);
	dataMessage.hasZos = true;
	return true;
}

bool MainDialog::readTimeStepData(ncdfDataMessage& dataMessage) {
	ncdfLog("[ncdf] readTimeStepData: timeIndex=%d, latLength=%d, lonLength=%d, ucurr.size=%zu\n",
		dataMessage.timeIndex, (int)dataMessage.latLength, (int)dataMessage.lonLength, dataMessage.ucurr.size());

	if (!dataMessage.latValues || !dataMessage.lonValues) {
		ncdfLog("[ncdf] readTimeStepData: latValues or lonValues is NULL\n");
		return false;
	}

	// Reuse vector capacity: assign() overwrites values in-place when capacity is sufficient.
	// If new file is larger, proactively free old buffers to avoid 32-bit heap fragmentation.
	size_t nbr_uv = dataMessage.latLength * dataMessage.lonLength;
	ncdfLog("[ncdf] readTimeStepData: nbr_uv=%zu, ucurr.cap=%zu\n", nbr_uv, dataMessage.ucurr.capacity());
	if (nbr_uv > dataMessage.ucurr.capacity()) {
		ncdfLog("[ncdf] readTimeStepData: capacity insufficient (%zu < %zu), freeing old buffers\n",
			dataMessage.ucurr.capacity(), nbr_uv);
		dataMessage.ucurr.clear(); dataMessage.ucurr.shrink_to_fit();
		dataMessage.vcurr.clear(); dataMessage.vcurr.shrink_to_fit();
		dataMessage.sst.clear(); dataMessage.sst.shrink_to_fit();
		dataMessage.salinity.clear(); dataMessage.salinity.shrink_to_fit();
		dataMessage.zos.clear(); dataMessage.zos.shrink_to_fit();
	}
	try {
		dataMessage.ucurr.assign(nbr_uv, ncdf_NOTDEF);
		dataMessage.vcurr.assign(nbr_uv, ncdf_NOTDEF);
	} catch (const std::bad_alloc&) {
		ncdfLog("[ncdf] readTimeStepData: OOM assigning ucurr/vcurr (%zu elements)\n", nbr_uv);
		return false;
	}
	dataMessage.sst.clear();
	dataMessage.salinity.clear();
	dataMessage.zos.clear();

	int ncid;
	int retval;

	// Reuse persistent ncid for same-file time step switches (avoids repeated nc_open/nc_close)
	wxString filenameStr = dataMessage.fileName;
	if (m_openedNcid >= 0 && m_openedFileName == filenameStr) {
		ncid = m_openedNcid;
		ncdfLog("[ncdf] readTimeStepData: reusing cached ncid=%d\n", ncid);
	} else {
		// Close old file if open
		if (m_openedNcid >= 0) {
			nc_close(m_openedNcid);
			m_openedNcid = -1;
		}
		const wxCharBuffer mbBuf = filenameStr.mb_str();
		size_t mbLen = strlen(mbBuf);
		char* filename = new char[mbLen + 1];
		strcpy(filename, mbBuf);

		retval = nc_open(filename, NC_NOWRITE, &ncid);
		if (retval != NC_NOERR) {
			retval = nc_open(filename, NC_NOWRITE | NC_NETCDF4, &ncid);
		}
		delete[] filename;
		if (retval != NC_NOERR) {
			ncdfLog("[ncdf] readTimeStepData: nc_open failed, retval=%d\n", retval);
			return false;
		}
		m_openedNcid = ncid;
		m_openedFileName = filenameStr;
		ncdfLog("[ncdf] readTimeStepData: opened file, ncid=%d\n", ncid);
	}

	// Discover variable IDs (with cached fallback)
	int u_varid = m_cached_u_varid;
	int v_varid = m_cached_v_varid;
	if (u_varid < 0 || v_varid < 0) {
		ncdfLog("[ncdf] readTimeStepData: cache miss, falling back to CF discovery\n");
		{
			const char* alt_std[] = {"surface_geostrophic_eastward_sea_water_velocity", "surface_eastward_sea_water_velocity", NULL};
			const char* long_hints[] = {"Eastward Current Velocity", "u-velocity component of current", NULL};
			const char* var_hints[] = {"u", "uo", "ugos", "current_u", "curr_u", "u10", "u100", NULL};
			u_varid = find_var_by_cf(ncid, "eastward_sea_water_velocity", alt_std, long_hints, var_hints);
		}
		{
			const char* alt_std[] = {"surface_geostrophic_northward_sea_water_velocity", "surface_northward_sea_water_velocity", NULL};
			const char* long_hints[] = {"Northward Current Velocity", "v-velocity component of current", NULL};
			const char* var_hints[] = {"v", "vo", "vgos", "current_v", "curr_v", "v10", "v100", NULL};
			v_varid = find_var_by_cf(ncid, "northward_sea_water_velocity", alt_std, long_hints, var_hints);
		}
	}

	bool hasUV = (u_varid != -1 && v_varid != -1);
	ncdfLog("[ncdf] readTimeStepData: hasUV=%d, u_varid=%d, v_varid=%d\n", (int)hasUV, u_varid, v_varid);

	// P2: Only read data types that are currently checked (visible).
	// Unchecked types will be read on-demand when the user clicks the checkbox.
	// Feature detection (识别) may additionally require fields (plan §1.2).
	ncdfOverlayFactory *pofFeat = pPlugIn ? pPlugIn->GetncdfOverlayFactory() : NULL;
	bool featUV = pofFeat && pofFeat->m_featureEngine.needUV();
	bool featTS = pofFeat && pofFeat->m_featureEngine.needTS();
	bool featZosNeed = pofFeat && pofFeat->m_featureEngine.needZos();
	bool featSpot = pofFeat && pofFeat->m_featureEngine.spotPending();   // verification probe: load all
	bool needCurrent = (pPlugIn && pPlugIn->m_bShowCurrentForce) || featUV || featSpot || m_kuroshioNeedUV;
	bool needSST = (pPlugIn && pPlugIn->m_bShowSeaTemp) || featTS || featSpot;
	bool needSal = (pPlugIn && pPlugIn->m_bShowSalinity) || featTS || featSpot;
	bool needZos = (pPlugIn && pPlugIn->m_bShowZos) || featZosNeed || featSpot;

	if (hasUV && needCurrent) {
		if (!readUVFromNC(ncid, u_varid, v_varid, dataMessage)) {
			ncdfLog("[ncdf] readTimeStepData: readUVFromNC failed\n");
			dataMessage.ucurr.clear();
			dataMessage.vcurr.clear();
		}
	} else {
		dataMessage.ucurr.clear();
		dataMessage.vcurr.clear();
	}

	if (ncid >= 0 && m_fileHasSeaTemp && needSST) {
		if (!readSSTFromNC(ncid, m_cached_sst_varid, dataMessage)) {
			ncdfLog("[ncdf] readTimeStepData: readSSTFromNC failed\n");
		}
	}

	if (ncid >= 0 && m_fileHasSalinity && needSal) {
		if (!readSalinityFromNC(ncid, m_cached_sal_varid, dataMessage)) {
			ncdfLog("[ncdf] readTimeStepData: readSalinityFromNC failed\n");
		}
	}

	if (ncid >= 0 && m_fileHasZos && needZos) {
		if (!readZosFromNC(ncid, m_cached_zos_varid, dataMessage)) {
			ncdfLog("[ncdf] readTimeStepData: readZosFromNC failed\n");
		}
	}

	ncdfLog("[ncdf] readTimeStepData: completed successfully\n");

	// Don't close ncid — it stays open for same-file time step switches
	return true;
}

wxDateTime MainDialog::GetDateFromHours(int hours_in){

	wxString baseDateTime = _T("2011-12-31 00:00:00");

	wxDateTime m_dt;
	m_dt.ParseDateTime(baseDateTime);

	wxTimeSpan m_ts = wxTimeSpan::Hours(hours_in);
	m_dt.Add(m_ts);

	return m_dt;
}
