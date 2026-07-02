# netCDF 3 vs netCDF 4 数据流对比

## 一、核心区别

| 特性 | netCDF 3 经典 API | netCDF 4 增强 API |
|------|-------------------|-------------------|
| **文件格式** | 自定义二进制 | HDF5 |
| **打开标志** | `NC_NOWRITE` | `NC_NOWRITE \| NC_NETCDF4` |
| **最大文件** | 2GB（需 `NC_64BIT_OFFSET`） | 无限制 |
| **压缩** | 不支持 | Deflate, Shuffle |
| **分组** | 不支持 | 支持 Groups |
| **字符串** | 固定长度 | 可变长度 |
| **数据类型** | 基础类型 | 扩展类型（uint, complex） |

## 二、数据流步骤对比

### 步骤1：打开文件

**netCDF 3:**
```cpp
nc_open(filename, NC_NOWRITE, &ncid);
```

**netCDF 4:**
```cpp
nc_open(filename, NC_NOWRITE | NC_NETCDF4, &ncid);
```

### 步骤2：查询文件格式（netCDF 4 特有）

```cpp
nc_type format;
nc_inq_format(ncid, &format);
// NC_FORMAT_CLASSIC, NC_FORMAT_64BIT_OFFSET, 
// NC_FORMAT_NETCDF4, NC_FORMAT_NETCDF4_CLASSIC
```

### 步骤3：查询文件结构（相同）

```cpp
nc_inq(ncid, &ndims_in, &nvars_in, &ngatts_in, &unlimdimid_in);
```

### 步骤4：查询维度（相同）

```cpp
nc_inq_dimid(ncid, "time", &timeid);
nc_inq_dimlen(ncid, timeid, &timelength);
```

### 步骤5：查询变量ID（基本相同，netCDF 4 支持分组路径）

**netCDF 3:**
```cpp
nc_inq_varid(ncid, "u", &u_varid);
```

**netCDF 4（支持分组）:**
```cpp
nc_inq_varid(ncid, "group_name/u", &u_varid);
```

### 步骤6：查询压缩信息（netCDF 4 特有）

```cpp
int shuffle, deflate, deflate_level;
nc_inq_var_deflate(ncid, u_varid, &shuffle, &deflate, &deflate_level);
```

### 步骤7：读取属性（相同）

```cpp
nc_get_att_float(ncid, NC_GLOBAL, "latitude_min", &val);
nc_get_att_float(ncid, u_varid, "scale_factor", &val);
```

### 步骤8：读取数据（相同）

```cpp
nc_get_var_float(ncid, lat_varid, lats);
nc_get_vara_float(ncid, u_varid, start, count, u_vals);
```

### 步骤9：查询分组信息（netCDF 4 特有）

```cpp
int ngrps;
nc_inq_grps(ncid, &ngrps, NULL);
```

### 步骤10：关闭文件（相同）

```cpp
nc_close(ncid);
```

## 三、关键 API 对比表

| 功能 | netCDF 3 API | netCDF 4 API |
|------|--------------|--------------|
| 打开文件 | `nc_open()` | `nc_open()` + `NC_NETCDF4` |
| 查询格式 | 不支持 | `nc_inq_format()` |
| 查询压缩 | 不支持 | `nc_inq_var_deflate()` |
| 创建分组 | 不支持 | `nc_def_grp()` |
| 查询分组 | 不支持 | `nc_inq_grps()` |
| 可变字符串 | 不支持 | `NC_STRING` |
| 无符号整型 | 不支持 | `NC_UINT` |
| 大文件支持 | `NC_64BIT_OFFSET` | 自动支持 |

## 四、当前插件代码分析

插件 `src/ncdf.cpp` 使用的是 **netCDF 3 经典 API**：

```cpp
// 当前代码（第261行）
nc_open(filename, NC_NOWRITE, &ncid);

// 支持 64位偏移格式需修改为：
nc_open(filename, NC_NOWRITE | NC_64BIT_OFFSET, &ncid);

// 支持 netCDF 4 需修改为：
nc_open(filename, NC_NOWRITE | NC_NETCDF4, &ncid);
```

## 五、文件格式检测

### 文件头识别

| 文件头字节 | 格式 | 插件支持 |
|------------|------|----------|
| `4E 43 44 46 01` | netCDF 3 经典 | ✅ |
| `4E 43 44 46 02` | netCDF 3 64位 | ❌（需标志） |
| `89 4E 43 44 46` | netCDF 4 | ❌（经典 API 无法读取） |

### 使用 ncdump 检测

```bash
ncdump -k file.nc
# 输出: classic / 64-bit offset / netCDF-4 / netCDF-4 classic model
```

## 六、总结

1. **netCDF 3**：简单、兼容、速度快，适合小文件
2. **netCDF 4**：功能强大、支持压缩和分组，适合大数据
3. **当前插件**：只支持 netCDF 3 经典格式
4. **扩展支持**：只需修改 `nc_open()` 的标志即可支持更多格式