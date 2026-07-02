/**
 * netCDF 3 经典 API 示例代码
 * 
 * 演示使用 netCDF 3 经典 API 读取海洋流场数据的完整流程：
 * 1. 打开 netCDF 文件
 * 2. 查询文件结构信息（维度、变量、属性数量）
 * 3. 获取维度 ID 和长度
 * 4. 获取变量 ID
 * 5. 读取全局属性和变量属性
 * 6. 读取变量数据（完整变量和切片）
 * 7. 关闭文件
 * 
 * 数据模型：潮流数据（u: 东向流速, v: 北向流速）
 * 维度结构：time × latitude × longitude
 * 
 * 编译命令：gcc ncdf3_example.cpp -o ncdf3_example -lnetcdf
 */

#include <stdio.h>
#include <stdlib.h>
#include <netcdf.h>

// 变量名称常量定义，对应 nc 文件中的变量名
#define LAT_NAME  "latitude"   // 纬度变量名
#define LON_NAME  "longitude"  // 经度变量名
#define TIME_NAME "time"       // 时间变量名
#define U_NAME    "u"          // 东向流速变量名
#define V_NAME    "v"          // 北向流速变量名

// 错误处理宏：打印错误信息并退出程序
#define ERR(e) { fprintf(stderr, "Error: %s\n", nc_strerror(e)); return 1; }

/**
 * 主函数：演示 netCDF 3 经典 API 的完整数据流
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组，argv[1] 为 nc 文件路径
 * @return 0 表示成功，非 0 表示失败
 */
int main(int argc, char **argv) {
    // 检查命令行参数数量
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <nc_file>\n", argv[0]);
        return 1;
    }

    /* ========== 变量声明 ========== */
    int ncid;                    // netCDF 文件 ID
    int time_varid, lat_varid, lon_varid;  // 维度变量 ID
    int u_varid, v_varid;        // 数据变量 ID（u 和 v）
    int retval;                  // API 返回值

    // 文件结构信息
    int ndims_in;                // 维度数量
    int nvars_in;                // 变量数量
    int ngatts_in;               // 全局属性数量
    int unlimdimid_in;           // 无限制维度的 ID（-1 表示无）

    /* ========== 步骤1: 打开文件 ========== */
    // 使用 NC_NOWRITE 标志以只读方式打开文件
    // 若要支持大于 2GB 的文件，需添加 NC_64BIT_OFFSET 标志
    if ((retval = nc_open(argv[1], NC_NOWRITE, &ncid)))
        ERR(retval);
    printf("[netCDF 3] 文件已打开\n");

    /* ========== 步骤2: 查询文件结构 ========== */
    // nc_inq() 获取文件的基本结构信息
    // 参数：ncid, 维度数, 变量数, 属性数, 无限制维度ID
    if ((retval = nc_inq(ncid, &ndims_in, &nvars_in, &ngatts_in, &unlimdimid_in)))
        ERR(retval);
    printf("[netCDF 3] 维度数: %d, 变量数: %d, 属性数: %d\n", 
           ndims_in, nvars_in, ngatts_in);

    /* ========== 步骤3: 查询维度信息 ========== */
    int latid, lonid, timeid;    // 维度 ID
    size_t timelength;           // 时间维度长度
    size_t latlength;            // 纬度维度长度
    size_t lonlength;            // 经度维度长度

    // 通过名称查询维度 ID
    retval = nc_inq_dimid(ncid, "time", &timeid);
    // 通过维度 ID 查询维度长度
    retval = nc_inq_dimlen(ncid, timeid, &timelength);

    retval = nc_inq_dimid(ncid, "latitude", &latid);
    retval = nc_inq_dimlen(ncid, latid, &latlength);

    retval = nc_inq_dimid(ncid, "longitude", &lonid);
    retval = nc_inq_dimlen(ncid, lonid, &lonlength);

    printf("[netCDF 3] 维度: time=%zu, lat=%zu, lon=%zu\n", 
           timelength, latlength, lonlength);

    /* ========== 步骤4: 查询变量ID ========== */
    // 通过变量名称查询变量 ID，后续读取数据需要使用这些 ID
    if ((retval = nc_inq_varid(ncid, U_NAME, &u_varid))) ERR(retval);
    if ((retval = nc_inq_varid(ncid, V_NAME, &v_varid))) ERR(retval);
    if ((retval = nc_inq_varid(ncid, TIME_NAME, &time_varid))) ERR(retval);
    if ((retval = nc_inq_varid(ncid, LAT_NAME, &lat_varid))) ERR(retval);
    if ((retval = nc_inq_varid(ncid, LON_NAME, &lon_varid))) ERR(retval);
    printf("[netCDF 3] 变量ID获取完成\n");

    /* ========== 步骤5: 读取全局属性 ========== */
    float firstGridPointLat;     // 第一个网格点的纬度
    float firstGridPointLon;     // 第一个网格点的经度
    float lastGridPointLat;      // 最后一个网格点的纬度
    float lastGridPointLon;      // 最后一个网格点的经度
    float scale_factor;          // u 变量的缩放因子（属性）

    // nc_get_att_float() 读取浮点型属性
    // 参数：ncid, 变量ID(NC_GLOBAL表示全局属性), 属性名, 存储值的指针
    retval = nc_get_att_float(ncid, NC_GLOBAL, "latitude_min", &firstGridPointLat);
    retval = nc_get_att_float(ncid, NC_GLOBAL, "longitude_min", &firstGridPointLon);
    retval = nc_get_att_float(ncid, NC_GLOBAL, "latitude_max", &lastGridPointLat);
    retval = nc_get_att_float(ncid, NC_GLOBAL, "longitude_max", &lastGridPointLon);
    retval = nc_get_att_float(ncid, u_varid, "scale_factor", &scale_factor);

    printf("[netCDF 3] 属性读取完成\n");

    /* ========== 步骤6: 读取变量数据 ========== */
    // 分配内存存储维度坐标数据
    float *lats = (float*)malloc(latlength * sizeof(float));  // 纬度数组
    float *lons = (float*)malloc(lonlength * sizeof(float));  // 经度数组
    int *times = (int*)malloc(timelength * sizeof(int));      // 时间数组

    // nc_get_var_float() 读取整个变量的浮点数据
    nc_get_var_float(ncid, lat_varid, lats);
    nc_get_var_float(ncid, lon_varid, lons);
    // nc_get_var_int() 读取整个变量的整型数据
    nc_get_var_int(ncid, time_varid, times);

    // 设置数据切片参数（读取第一个时间步的所有数据）
    size_t start[] = {0, 0, 0};                    // 起始索引 [time, lat, lon]
    size_t count[] = {1, latlength, lonlength};    // 读取数量 [time, lat, lon]
    
    // 分配内存存储 u 和 v 流速数据
    float *u_vals = (float*)malloc(latlength * lonlength * sizeof(float));
    float *v_vals = (float*)malloc(latlength * lonlength * sizeof(float));

    // nc_get_vara_float() 读取指定区域的数据切片
    // 参数：ncid, 变量ID, 起始索引, 读取数量, 存储数据的指针
    nc_get_vara_float(ncid, u_varid, start, count, u_vals);
    nc_get_vara_float(ncid, v_varid, start, count, v_vals);

    printf("[netCDF 3] 数据读取完成: %zu 个网格点\n", latlength * lonlength);

    /* ========== 步骤7: 关闭文件 ========== */
    // 关闭 netCDF 文件，释放资源
    nc_close(ncid);
    printf("[netCDF 3] 文件已关闭\n");

    // 释放动态分配的内存
    free(lats);
    free(lons);
    free(times);
    free(u_vals);
    free(v_vals);

    return 0;
}