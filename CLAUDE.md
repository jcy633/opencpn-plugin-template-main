## Hard Constraints
- 插件编译必须使用 `d:\opencpn-plugin-template-main\build` 目录
- 编译命令：`cd build && "D:\VS\MSBuild\Current\Bin\MSBuild.exe" "ncdf_pi.vcxproj" -p:Configuration=Release -p:Platform=Win32 -t:Build -v:minimal`
- 编译输出DLL位于：`d:\opencpn-plugin-template-main\build\Release\ncdf_pi.dll`（注意是build/Release/，不是根目录的Release/）
- 部署：将 `build\Release\ncdf_pi.dll` 复制到 `C:\Users\季曹阳\AppData\Local\opencpn\plugins\`
- 部署完成后启动OpenCPN：`start "" "C:\Program Files (x86)\OpenCPN\opencpn.exe"`（直接用exe，不用快捷方式）
- 如果DLL被占用，先 `taskkill //f //im opencpn.exe` 再部署
- rebuild.bat 已配置自动关闭OpenCPN、部署、重启
- **build/ 是 CMake 生成的工程**。改了 `Plugin.cmake` / `CMakeLists.txt` 后必须重新生成，且**必须在 `build/` 目录里跑并显式指定平台**：`cd build && cmake -A Win32 ..`。裸 `cmake .` 会①默认生成成 x64 冲掉 Win32 配置、②把产物吐到项目根目录污染源码树。生成后 `grep -oE 'Include="[^"]*\|[^ \"]*' build/ncdf_pi.vcxproj | sort -u` 必须出现 `Release|Win32`
- 源码文件改动后不需要重新生成 CMake（只加/删源文件才需要）
- OpenCPN运行日志位于C:\ProgramData\opencpn\opencpn.log
- OpenCPN崩溃日志位于C:\Users\季曹阳\AppData\Local\CrashDumps\
- 插件的调试日志位于C:\ProgramData\opencpn\ncdf_debug.log
- 分析运行日志和崩溃日志时必须显示时间戳信息，便于追踪问题发生的时间点
- 渲染数据传递必须使用CurrentData结构体，禁止通过全局变量传递
- 配置状态（显示方向/强度）必须通过m_bShowCurrentDir/m_bShowCurrentForce持久化，禁止直接读取GUI控件
- 插件必须支持读取符合CF规范的nc4文件
- nc4文件读取必须采用按时间步长切块读取方式，仅在更换时间节点时读取对应数据
- 时间步数据切换时必须释放上一次读取的数据，防止堆泄漏
- 读取全局属性时必须尝试多种属性名（如latitude_min、lat_min、min_lat），失败时从数据数组推断边界信息
- 坐标数组（latValues、lonValues）和U/V数据（ucurr、vcurr）使用前必须进行空指针检查
- 时间步切换时必须重置m_lastSelectedTimeIndex索引为-1
- 读取nc4文件时必须读取_FillValue属性替代硬编码的-32767
- 变量维度顺序必须动态检测，支持任意排列
- nc4文件读取必须支持double类型数据读取和转换
- 错误处理必须使用nc_close + return替代ERR宏
- 维度名必须支持"lat"/"lon"作为替代
- 目录树仅显示扩展名为.nc和.nc4的文件
- 选择目录后必须手动调用fillDirTree刷新目录树
- 在fillDirTree函数中必须使用Freeze()/Thaw()防止树控件更新时触发事件
- 调试日志必须使用自定义ncdfLog/ncdfLogW函数（基于fprintf），确保在Release版本中也能输出
- Windows路径必须使用\分隔符，避免混用/导致路径解析问题
- fillDirTree中调用DeleteAllItems()前必须断开事件连接，防止触发onTreeSelectionChanged事件导致崩溃
- 禁止在nc_get函数中使用wxString::mb_str()返回的临时对象指针，必须使用new char[]动态分配内存并在所有退出点正确释放
- ncdfDataMessage::copyFrom()中必须先赋值latLength、lonLength、timeLength，再进行内存分配
- onFileButtonClick函数中禁止重复调用fillDirTree（SetValue已触发onDirChanged）
- fillDirTree函数中禁止调用addChildren（避免nc文件读取）
- fillDirTree和addChildren函数中已重新启用SetItemData，但MyTreeItemData仅存储int索引（m_index），不存储ncdfDataMessage副本（避免深拷贝崩溃）
- onTreeSelectionChanged和readData通过data->m_index访问myDataVector[idx]，不再直接依赖MyTreeItemData的myData成员
- 树操作必须使用m_isTreeUpdating标志位保护，防止DeleteAllItems()触发的事件导致状态不一致
- nc4文件变量识别必须优先通过读取`standard_name`属性（u分量：eastward_sea_water_velocity，v分量：northward_sea_water_velocity，纬度：latitude，经度：longitude，时间：time），找不到时回退到硬编码名称匹配
- nc_get函数中必须添加函数启动、myDataVector.clear()前后、文件名处理过程的详细调试日志，便于定位崩溃步骤
- nc_get函数中必须检查find_dim_by_var返回的维度ID是否有效（不为-1）
- nc_get函数中必须为nc_inq_dimlen添加错误检查，防止使用无效维度ID导致崩溃
- nc_get函数中必须检查返回值，若为非零则跳过该文件，不处理无效数据
- 树控件中日期后面的括号里不显示时间，改为显示维度中的垂直高度（描述海洋的垂直分层）
- wxTreeCtrl::DeleteAllItems()会自动删除关联的wxTreeItemData对象，禁止在DeleteAllItems前手动delete，否则导致double-free崩溃
- readData函数中logFile在多个提前返回分支中未关闭，会导致文件句柄资源泄漏；必须在函数退出前统一检查并关闭logFile
- ncdfDataMessage::copyFrom方法只复制了指针地址，没有重新分配内存复制数据，会导致两个对象析构时对同一指针调用两次free，造成double free错误；必须为每个动态分配指针重新分配内存并拷贝数据
- 位图缓存仅在缩放级别变化或视口超出数据覆盖范围时重建，数据区域内移动地图时复用位图
- 渲染方式切换通过插件对话框中m_checkBoxDCurrent（控制m_bShowCurrentDir）和m_checkBoxBmpCurrentForce（控制m_bShowCurrentForce）复选框实现，状态改变需实时保存并触发刷新
- 颜色渲染必须使用直接纹理创建（CreateGLTexture），避免wxImage中间层，直接写入RGBA数据数组
- 箭头绘制必须使用LineBuffer缓存系统预计算箭头形状，通过glDrawArrays批量绘制
- 纹理创建必须使用数据原生网格尺寸（latLength, lonLength），支持自动降采样/升采样到1024x1024限制
- 纹理绘制必须采用分块绘制策略，根据视图缩放计算块大小，处理非线性投影
- 颜色映射必须使用GetSeaCurrentGraphicColorRaw直接写入RGBA，避免wxColour对象开销
- 箭头颜色必须随速度变化，使用GetSeaCurrentGraphicColorRaw获取颜色
- 渲染时必须启用OpenGL抗锯齿（GL_LINE_SMOOTH和GL_BLEND），设置glHint(GL_LINE_SMOOTH_HINT, GL_NICEST)
- 纹理上传必须显式设置 `glPixelStorei(GL_UNPACK_ALIGNMENT, …)`（详见「渲染与着色器约束」）。**数据/物理纹理用 `GL_NEAREST`，插值由着色器自己做**（B 样条 / 双线性 / 点取色）；只有 LUT 用 `GL_LINEAR`
- 纹理绘制需支持全球数据重复渲染（repeat = true），使用GetCanvasLLPix转换像素坐标为经纬度映射到纹理坐标
- 墨卡托投影下（vp->rotation == 0 && vp->m_projection_type == PI_PROJECTION_MERCATOR），经度方向设为线性（xsquares=1）减少分块数量
- 全球数据分块跨越日期变更线时，通过u1 - u0 > .5时u1--校正纹理坐标相位，避免纹理撕裂
- 纹理绘制前检查块的纹理坐标是否在[0,1]范围内，跳过完全在纹理外的块（repeat模式除外）
- 行缓冲采用j = !j交替读写优化，替代整行memcpy复制
- ~~纹理生成使用next_power_of_two()计算最近的2的幂尺寸~~ **已废弃**：纹理直接按数据原生网格尺寸（ni+2, nj+2）建，不再做 POT 填充
- ~~新增m_texDataDim[2]记录实际数据尺寸，通过potNormX/potNormY归一化数据坐标到POT纹理空间~~ **已废弃**：`potNormX/potNormY` 仍留在 `ComputeTileUVs`/`MakeDataGeo` 里，但 `dataDim == glDim` 恒成立，取值恒为 1.0
- 纹理缓冲区用 `memset` 初始化为 0，确保未写入区域为透明黑色（这条仍有效）
- 箭头角度计算必须包含vp->rotation：double ang = atan2(g2data.vcurr[mi], -g2data.ucurr[mi]) + vp->rotation
- 箭头大小随速度缩放：double scale = wxMax(1.0, vkn / 5.0)
- DrawGLLine函数中禁止使用逐行glPushAttrib/glPopAttrib调用，仅保留必要的glColor4ub、glLineWidth和glBegin/glEnd
- 颜色索引查找必须使用二分查找算法，将时间复杂度从O(n)降低到O(log n)
- 海流箭头绘制支持固定间距模式（通过m_bFixedSpacing标志切换），解决不同缩放级别下箭头密度不一致问题
- 颜色映射支持动态切换（ColorMapType枚举：CurrentMap、WindMap、GenericMap、SeaTempMap），通过m_colorMapType成员变量控制
- 固定间距箭头绘制需添加间距计算的安全防护，避免除零错误
-可以参考grib插件的相关功能的代码，对本插件的功能的代码进行优化。
## 渲染与着色器约束
### GLSL 源码
- **着色器字符串（`s_vert`/`s_frag`）里只能写 ASCII 注释**。MSVC 按 GBK（代码页 936）读源码：UTF-8 中文字符的尾字节 0x80–0xBF 落在 GBK 次字节范围 0x40–0xFE 内，会把紧随其后的 ASCII 字符（尤其 `\`）并成一个 GBK 双字节字符吞掉，`\n` 转义被毁、字符串边界错乱 → GLSL 语法错误 → 着色器编译失败 → 整屏白屏
- `compile_shader_gl` 里的非 ASCII 消毒是**运行时**的，救不了 C 编译期的破坏。防止手段只有一个：注释写英文
- C++ 代码里的中文注释不受影响（不在 GLSL 字符串内），但同一文件会有 C4819 警告，属正常
### 绘制顺序（必须保持）
- 每个数据图层内部：**海色 → 补空白**（`landMode==4`）
- 所有数据图层画完后，`DoRenderncdfOverlay` 末尾**统一画一次** GSHHG 陆地收尾，永远是最后一笔
- 陆地不需要在每个数据层内部画：补空白自己查掩膜避开陆地，四层全开时逐层画陆地是纯浪费（8 个 pass/帧）
- 海底底色板已移除（铺满整片的做法用户判定无用）；海底色 `#A9C2EF` 仅用于补空白的无源水域分支。**海色（数据插值色场）与海底色是两个概念**，不要混用
### 插值规则
- **2x2 邻域内只要有一个陆地格点，该片元完全不插值**，直接输出透明跳过
- 4x4 有陆地但 2x2 干净 → 对物理量做**双线性插值**（Case 2，保留）
- 4x4 全海 → **B 样条重建**（Case 1）
- 补空白只在「海色 pass 留了空隙 **且** GSHHG 判为海」时着色：取得到海点颜色 → 最近海点的颜色；取不到源颜色（如内陆水体）→ **海底色 `#A9C2EF` = RGB(169,194,239)**。禁止编造海洋数据颜色
- 海底色只在上面这种「无源水域」处出现，不要拿它参与任何插值或充当海色
### 陆海掩膜（GSHHG）查询
- 查掩膜**必须绕道经纬度**，且经度先规范化到 [-180,180)：`dLon = -180.0 + mod(dLon + 180.0, 360.0)`
- **禁止用 dataUV 的仿射直接查掩膜**：全世界数据的 dataTex 是 `GL_REPEAT`，`vUV.x` 在日期变更线 1→0 绕回，非周期仿射会让掩膜 UV 平移一整格宽度 → 陆地判定翻转
- 掩膜纹理是 `GL_LUMINANCE`（1 字节/格），宽度常非 4 倍数，上传前必须 `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)`
- 掩膜文件本身是**闭合**的（`nlon = 43201 = 43200+1`，首尾各一份）。历史上是精层+粗层两级互补，**现在只建一层**（`s_landLayer`），没有挖空/接缝问题
### 分块绘制
- `ComputeTileUVs`（数据）与 `ComputeLandUVs`（陆地）的分块数**必须一致**（xs、ys 下限都是 4）。不一致时两边分段线性 UV 逼近误差不同，海岸线会被错开一圈
### 性能（硬性）
- **平移 / 缩放过程中绝对不允许出现卡顿**。任何「按视口重建纹理」的方案都不可接受——陆地图层、颜色纹理只能在**数据变更 / 时次切换 / 勾选图层**时构建，平移缩放时必须原样复用
- 分帧构建也不行：新区域闪旧海岸线同样不可接受
### 陆海掩膜分辨率
- 分辨率按 **nc 文件覆盖范围**自适应，不按视口。`BuildLandLayer` 从掩膜原生格距起，装不下 `GL_MAX_TEXTURE_SIZE` 或 `kMaxCells`（1.28 亿格）就把格距翻倍再试
- 区域文件（≲90°×90°）保持 30″ 原生；**全球数据会被降到 2′（3.7 km）**——43201×21601 = 9.3 亿格 = 933 MB，单张纹理装不下。**用户已确认全球 2′ 可接受，不要再为此引入分块加载（会卡顿）或大纹理（933 MB）**
- 精层（15″）已停用，只建一层粗层，不存在两层挖空接缝
### 四种颜色图的资源复用
- **颜色图的显示与否只能由它自己的复选框决定**（`m_bShowCurrentForce` / `m_bShowSeaTemp` / `m_bShowSalinity` / `m_bShowZos`）。即使数据/纹理提前准备好，没勾选也必须隐藏——**禁止「先准备好就先显示」的渐进式显示**
- 加速只能作用于**准备阶段**（网格、B 样条系数、纹理上传），不能改变显示时机
- **GSHHG 陆地图层全局只建一次**（`s_landBuilt` 守卫），海流/海温/海盐/海高四层共用；不要按图层重复建
- 各图层的**数据纹理 + 物理纹理**各自独立、按需构建：`prepareAllOverlays` 只准备勾选中的图层，GL 纹理在首次渲染时才上传（需要 GL 上下文）
- 全球数据下单层约占 70 MB（数据 35 MB + 物理 35 MB），四层全开约 280 MB

### 其他硬性要求
- `physicalTex` 在补空白 pass（`landMode==4`）下**标量模式也要绑定**，不能只在 `vectorMode` 时绑
- **禁止对系数纹理 `texData` 调 `FillTextureGaps`**：它会把无数据格点填成海洋值并把 alpha 改写成 255，既毁掉 `FillLandFractionChannel` 的 `a == 1` 陆地标记，也让着色器的 2x2 判陆彻底失效
- 源数据缺测的填补属于 **nc 数据处理阶段**，不在渲染里做
- 运行时诊断开关在 `C:\ProgramData\opencpn\ncdf_flags.txt`（`sea`/`land`/`fill`/`tileX`/`tileY`/`dbgFill`），每 30 次渲染重读一次，改完存盘并**动一下地图**才生效
## OpenCPN OpenGL 环境约束
- OpenCPN使用OpenGL 2.1（GLSL 1.20），**不支持RGBA16F等浮点纹理格式**
- 核心功能：基础纹理、固定管线、GLSL 1.20着色器
- VBO（顶点缓冲对象）：作为扩展使用（GL_ARB_vertex_buffer_object）
- FBO（帧缓冲对象）：使用扩展版本 GL_EXT_framebuffer_object
- 浮点纹理（GL_ARB_texture_float）：**不可用**，所有纹理必须使用RGBA8（unsigned byte）
- 纹理上传必须使用GL_RGBA + GL_UNSIGNED_BYTE格式
## Engineering Conventions
- 循环变量应使用size_t类型避免有符号/无符号不匹配
- C++17及以上环境中禁止使用废弃的register关键字
- 数组分配必须使用calloc(n, sizeof(type))确保元素数量和大小正确
- 内存释放后必须将指针置为NULL避免野指针
- 数据填充顺序必须根据维度顺序调整循环嵌套

## Lessons Learned
- 硬编码全局属性名会导致部分nc4文件无法读取边界信息
- 时间步切换时未重置索引会导致数据读取错误
- 坐标数组和U/V数据缺少空指针检查会导致运行时崩溃
- 使用ERR宏进行错误处理会导致资源未释放
- 硬编码维度顺序会导致部分nc4文件数据填充错误
- 硬编码-32767作为填充值会导致数据精度问题
- 类型转换时未使用临时变量会导致_FillValue栈损坏
- wxTextCtrl::SetValue()不会触发wxEVT_COMMAND_TEXT_UPDATED事件，需手动刷新目录树
- 编译完成后必须检查DLL文件的生成时间是否与当前时间接近，若时间不符说明编译未成功，需删除缓存文件重新编译
- 编译完成后部署流程：将新编译的ncdf_pi.dll复制到"C:\Users\季曹阳\AppData\Local\opencpn\plugins"下
- MyTreeItemData构造函数中myFile成员变量未正确赋值会导致文件名信息丢失和崩溃
- 颜色位图渲染（RenderncdfCurrentBmp）计算量大，频繁重建导致地图移动卡顿
- 使用glDrawPixels逐帧上传位图数据到GPU会导致渲染性能低下，必须使用OpenGL纹理（glTexImage2D）替代
- 线性查找颜色索引在颜色映射表较大时效率较低，二分查找是更优方案
- ncdfDataMessage::getInterpolatedValue等函数按值传递会导致每帧120K次数据复制卡死，必须用const引用
- ncdf数据速度分量符号与GRIB相反：粒子方向用atan2(vx, vy)，不是atan2(-vx, -vy)
- Vertex arrays批量渲染会导致堆损坏（clearBmp/ClearParticles竞争），必须用immediate mode GL
- 粒子速度缩放：5x太大跳过涡流，2x是平衡点
- 粒子密度滑块用wxSL_MIN_MAX_LABELS|wxSL_VALUE_LABEL显示标签
- 非线性密度公式：density = 4.0 * exp(slider - 7)
- GRIB粒子流动感关键：lip/sip双缓冲插值，整条轨迹随m_Run平滑移动
- glDeleteTextures等GL函数必须在拥有GL上下文的线程（渲染线程）中调用；setData()在文件读取线程中执行，Invalidate()中禁止调用GL函数，必须延迟到渲染线程中删除纹理
- **着色器字符串里的中文注释会被 GBK 吞字符**，导致 GLSL 编译失败、整屏白屏。`compile_shader_gl` 的运行时消毒无能为力（破坏发生在 C 编译期）。现象：`opencpn.log` 出现 `[shader] compile error: 0(117) : error C0000: syntax error` 且行号对不上源码
- **泛洪/最近邻传播必须每趟用源标记快照**。一趟扫描内原地改写 `src`，刚填好的格点会立刻成为新源，值沿扫描方向（向右向下）一路传出去，画面上是成片长横线。`FillTextureGaps` 长期"实现不成功"就是这个原因。判据：日志里 `2 passes max` 却填了 250 万格
- **不要依赖"事后画陆地去盖补空白"**。哪一侧覆盖差一点点，海色就被顶到海岸线外面多出一圈；补空白必须自己查掩膜
- **掩膜查询用 dataUV 仿射会在日期变更线出现硬竖边**，且深处陆地被误判成海。必须绕道经纬度并规范化经度。判据：硬边位置固定在日期变更线，且与 `tileX` 无关
- **调试可视化开关必须放在它要展示的那个判定之后**。`dbgFill` 曾放在取值检查之前，把"会画的区域"当成"实际画的区域"显示，造成"内陆全红"的假象，误导排查
- **连续猜错两次就该停手改成可二分的开关诊断**。本次定位靠一条观测：硬边在日期变更线且与 `tileX` 无关。用 `sea`/`land`/`fill` 开关二分比反复改代码猜因果快得多
- `_FillValue = NaNf` 会让缺测判定失效（`x != NaN` 恒为真）。已改为「数据为 NaN 一律缺测；fill 非 NaN 时再比对相等」
- GSHHG 栅格化两个坑：① level 5/6 是南极（冰架前缘/接地线）不是水；② 多边形经度是 0..360 展开的，必须先沿路径解缠再扫描线，否则跨本初子午线的多边形会被填成覆盖半个地球

## 动画功能（已停用）
- 流场动画**当前停用**。`ncdf_animate.cpp` 保留源码并在工程里编译（`m_animate` 成员要链接），但取数入口被 `#if 0` 关掉，`ncdf.cpp` 里 `m_animate.Init/ComputeDisplacementMap/Timer.Start` 已注释
- 要重新启用：把 `ncdfoverlayfactory.cpp` 里 `bool animateActive = false;` 后面的 `#if 0` 改成 `#if 1`，并恢复 `ncdf.cpp` 里被 `// [动画已停用]` 注释的三行
- **两套动画实现曾并存**（`ncdf_animate` / `ncdfanimate`，同一对专利的两套代码），`ncdfanimate.*` 与无人引用的 `ncdf_animate_shader.*` 已删除。若以后要 GL 加速版，从 git 历史找

## 参考文档
- `docs/交接文档-黑潮主轴与陆海掩膜.md`：黑潮主轴提取算法（Wang2026 流线约束最大流速法）与 GSHHG 陆海掩膜的完整设计、`.landmask.bin` 文件格式、构建部署流程、以及**已回退改动清单**（动手前先看，避免重蹈覆辙）

- koa-connect wrapper caused ctx leaks, so native rewrite is required