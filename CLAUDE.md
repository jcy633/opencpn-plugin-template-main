## Hard Constraints
- 插件编译必须使用 `d:\opencpn-plugin-template-main\build` 目录
- 编译命令：`cd build && "D:\VS\MSBuild\Current\Bin\MSBuild.exe" "ncdf_pi.vcxproj" -p:Configuration=Release -p:Platform=Win32 -t:Build -v:minimal`
- 编译输出DLL位于：`d:\opencpn-plugin-template-main\build\Release\ncdf_pi.dll`（注意是build/Release/，不是根目录的Release/）
- 部署：将 `build\Release\ncdf_pi.dll` 复制到 `C:\Users\季曹阳\AppData\Local\opencpn\plugins\`
- 部署完成后启动OpenCPN：`start "" "C:\Program Files (x86)\OpenCPN\opencpn.exe"`（直接用exe，不用快捷方式）
- **⚠️ 覆盖 DLL 不一定生效**：如果 OpenCPN 之前**拒绝过**某个 `ncdf_pi.dll`（例如误放过 64 位版本），之后即使正常覆盖 32 位 DLL，OpenCPN 也可能一直跑旧的缓存副本，**必须在插件管理器里手动重新导入**。判据：`strings .../ncdf_pi.dll | grep "<新加的日志串>"` 能搜到，但 `ncdf_debug.log` 里就是不出现 —— 说明跑的是旧副本。改完代码后若日志行为对不上，先怀疑这一条，别去怀疑代码
- **⚠️ 插件加载期不要触发 MainDialog**：`MainDialog` 是**点工具栏才构造的**。若在 `ncdf_pi::Init()` 里调 `OnToolbarToolCallback(0)` 强行建面板（自动化测试入口就是这么做的），加载插件时就会走完整个面板构造 + 数据加载链路。2026-10-29 疑似因此卡死。判据：`C:\ProgramData\opencpn\ncdf_seamt_auto.txt` 存在 → 每次加载都触发。**排查/退出前务必删掉该请求文件**
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
## 论文与数据源（两种主轴提取方法）

| 方法 | 论文 | 本地路径 | 源数据 |
|---|---|---|---|
| **Wang2026**<br>流线约束最大流速法<br>（黑潮**上游**：115–132°E / 15–32°N） | Wang et al. 2026, *Scientific Data* 13:915 | `E:\my paper\Wang2026_黑潮轴与边界三维日尺度数据集_流线约束最大流速法_1993-2024东海与吕宋海峡日尺度三维轴线及左右边界\`<br>· `Wang2026_ScientificData_黑潮轴与边界三维日尺度数据集.pdf`<br>· `官方代码_Kuroshio-axis-and-boundaries\`（MATLAB 原版） | **GLORYS12V1** 再分析，1/12°，50 层，1993–2024<br>Copernicus Marine doi 10.48670/moi-00021<br>掩膜用 ETOPO 2022 |
| **SEAMT**<br>流线集合最小 T 参数法<br>（黑潮**下游**：日本以南 / 延伸体） | Udalov 2026, *Ocean Modelling* 202:102730 | `E:\my paper\Udalov2026_急流核心提取_SEAMT流线集合分析_对流线集合求最小T参数定位黑潮及延伸体射流核\`<br>· `1-s2.0-S1463500326000545-main.pdf`<br>· `_extract_…txt`（英文抽取）、`中文译文_….md`、`核心算法解读_….md` | **CMEMS/AVISO L4** `SEALEVEL_GLO_PHY_L4_MY_008_047`<br>卫星测高地转流速，1/8°，1993–2024 |

**两者适用域不重叠（上游 vs 下游），不要混用。**

| 方法 | 验证状态 |
|---|---|
| **SEAMT** | ✅ **已成功**（2026-10-29）：用论文原始数据源（AVISO L4）+ 原始参数，6 个日子复现正文发表数值——核心量 T/L/ū 偏差 **0.06–0.2%**，13 个子射流对上 **11 个**（纬度偏差 ≤0.07°，网格 11.5 km）。见 `docs/SEAMT…md` §15 |
| **Wang2026** | ⏳ **进行中**（2026-10-29）：与发布数据集比对，整体中位 **1.88 km**（网格 8.5 km），但 B/C 段 42 km 待查。见 `docs/Wang2026主轴移植验证-排查记录.md` |

SEAMT 的方法论与实施修订见 `docs/SEAMT急流核心提取-方法论与实现方案.md`。

**验证用可复现的发表数值**（Udalov 2026 正文给到小数位，可逐位对表）：

| 日期 | 论文发表值 |
|---|---|
| 1993-01-01（平直日） | SEAMT T=35.1 d、L=3314 km、ū=1.10 m/s |
| 1993-07-19（强涡日） | SEAMT T=52.9 d、L=4344 km、ū=1.0 m/s |
| 1996-06-21 | 4 子射流：30.752°/71.50、30.427°/83.10、30.159°/112.25、30.000°/97.85 |
| 1996-09-11 | 3 子射流：30.216°/64.00、35.066°/45.80、35.272°/65.55 |
| 2010-12-24 | 3 子射流：33.749°/37.35、34.134°/60.85、34.173°/57.75 |
| 2021-11-26 | 3 子射流：32.395°/58.65、37.342°/50.65、37.596°/52.20 |

复现时须用论文原始参数：Γ_in=130°E、Γ_out=160°E、P₀ 带 28–31°N、141 点、Δt=0.03 d。

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
- **Win32 进程地址空间只有 2 GB**（不是物理内存）。四种颜色图的原始 double + 网格 + 系数峰值约 500 MB，看似宽裕，但 4 路并行算系数各还有约 40 MB 瞬时缓冲，叠加 OpenCPN 自身与 GL 驱动占用后会顶到上限。**`std::vector::clear()` 不归还容量**——必须用 `std::vector<T>().swap(v)` 才真正释放。2026-10-28 就是这个原因导致四类系数全部 `bad allocation`、颜色图加载不出来
- 加载时预准备该时次**所有**数据类型（不只勾选中的），可消除「首次勾选卡一下」；代价是 CPU 内存约 336 MB（四类网格+系数）
- `ncdfLogMem` 必须统计**全部**缓冲区（原始数据/网格/系数）并记录峰值，否则看不到真实内存水位
- **并发可用，但同步原语必须用 POD**：真正的卡死原因是 `static std::mutex`（非平凡类型，MSVC 函数内 static 走 guard+TLS 的线程安全初始化），而 `ncdfLog` 在**插件加载期**就会被调用，那套机制尚未就绪 → 挂死。改用 `static volatile LONG` + `InterlockedCompareExchange` 自旋锁即正常。同理：插件加载期会执行到的代码里，不要用任何非平凡类型的函数内/文件作用域 static
- 四层数据准备（`prepareAllGrids` / `prepareCoeff` 链）已并行化；`B 样条预滤波并行` 同理可行
- **自旋锁/互斥锁的提前 return 必须释放锁**，否则下次调用永久死锁
- **`glDrawArrays` 是异步的，CPU 侧计时测不到 GPU 开销**。渲染分段计时里 sea/fill/land 显示 0.00 ms 不代表没花时间，只代表 CPU 不等它。要定位渲染卡顿得用别的办法（如关掉绘制对比）
- **改 C 字符串里的 `\n` 转义时不要用 Python 脚本**：shell/Python 会把 `\\n` 折叠成真实换行，把字符串字面量截断成两行，编译报 `C2001: 字符串字面量中的换行符`。直接用编辑器改。本次踩了 4 次
### 陆海掩膜分辨率
- 分辨率按 **nc 文件覆盖范围**自适应，不按视口。`BuildLandLayer` 从掩膜原生格距起，装不下 `GL_MAX_TEXTURE_SIZE` 或 `kMaxCells`（1.28 亿格）就把格距翻倍再试
- 区域文件（≲90°×90°）保持 30″ 原生；**全球数据会被降到 2′（3.7 km）**——43201×21601 = 9.3 亿格 = 933 MB，单张纹理装不下。**用户已确认全球 2′ 可接受，不要再为此引入分块加载（会卡顿）或大纹理（933 MB）**
- 精层（15″）已停用，只建一层粗层，不存在两层挖空接缝
- **掩膜统一用 30″ 全球层，不用 15″ 那份**（2026-10-29 用户决定）。15″ 只有 12–36°N / 112–136°E，**盖不住 SEAMT**（`lonOut=142.0`、子射流到 37.6°N），只勉强够 Wang2026。`loadLandMaskFiles` 现在主动丢弃 15″（只在还有别的候选时丢），并把 `30s` 排最优先。**换掩膜不影响性能**——陆海判定只在撒种子时调用
- `BuildLandLayers` **从「只建 coarse」改成「有 coarse 用 coarse，否则用 fine」**：原先是 `db59067` 验证「陆地不正常」时留下的调试代码，掩膜只剩一份后 `coarse=NULL` 就一层都不建，症状是陆地被涂成海底色
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
- **测试型 / 使用型开关**在 `C:\ProgramData\opencpn\ncdf_mode.txt`：写 `test` 才启用自动化测试入口，写 `use`（或文件不存在，**默认**）时哪怕残留了 `ncdf_seamt_auto.txt` 也完全不生效。判定统一走 `ncdfAutoTestRequested()`。目的：把「测试入口」和「日常使用」彻底隔开，别让测试把插件弄坏
- `ncdf_flags.txt` 里的 `kuro*` 开关现在缓存 2 秒（`kuroFlagInt`），改完存盘停 2 秒重跑即生效。**新加开关时同样要注意别在热路径做 I/O**
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
- **任何"取数"路径（提取、诊断）都不得调 `setData(dataChanged=true)`**：它会 `clearCoefficients()` 删掉四层 GPU 纹理，而重建只在 `onTreeSelectionChanged` 的 Phase 2 做 —— 颜色图直接消失。而 Phase 2 会 `swap` 释放 `myMessage.ucurr/vcurr`（数据活在 float 网格里），`hasCurrent()` 恒为假，所以这个分支**每次都会进**。取数只需 `readTimeStepData`，不要动 overlay 状态
- **`kMPerDegLat` 是米/度**。用它算出的距离是**米**，当公里报会得出"偏移 3004 km"这类不可能的数字。涉及物理量的诊断先核对量纲
- **收敛性检验必须用超集**：拿"更密但不同格"的采样去对照，指标可能反而变差，检验完全失效。加密集合要用间距减半（`N' = 2·(N−1)+1`）；参数扫描时确认自变量**真的**在变
- **`read_var_att_text` 必须认 `NC_STRING`**：NetCDF-4 的字符串属性是变长串，`nc_inq_attlen`+`nc_get_att_text` 对它必失败，导致所有 `standard_name` 读不出、只能退到变量名匹配。要按 `nc_inq_atttype` 分流；`nc_get_att_string` 的参数是 `char**`——**调用方分配指针数组**，用完 `nc_free_string`
- **`nc_get_vara_*` 不做 CF 解包**：只做数值类型转换，**不**应用 `scale_factor`/`add_offset`。GLORYS 的 `uo/vo` 是 int16×0.00061、AVISO 的 `ugos/vgos` 是 int32×0.0001，不解包流速会变成 1300 m/s。缺失判定要在**原始值**上做，解包放在赋值时
- **Wang2026 的输出是「最大流速脊」不是「流线」**（切向偏离局部流向中位 9.4°），而 SEAMT 的输出**必须是流线**（0.02°）。速度脊可以横切流线保持在最快水上，所以它比任何流线都快、**不可能出现在候选池里**（池里装流线）。Wang2026 的偏差因此必然出在「骨架离速度脊多远」——垂断面校正只有 ±70 km，骨架离脊超过这个距离就只能吸附到局部极大
- **比对轴线必须用「点到折线垂距」**：参考产品常是定长重采样（如 180 点、点距 10–15 km），「点到点最近距离」有半个点距的**分辨率地板**，会把 1.88 km 抬成 4.13 km 误导结论
- **整体中位数会掩盖问题，必须分段看**：Wang2026 移植验证整体中位 1.88 km 很漂亮，但分段后 A/E <2 km 而 B/C 42 km
- **验证移植正确性要先复刻原版行为**，哪怕原版行为物理上不合理（如 MATLAB 把无效格点填 0 让流线穿过陆地）。先证明"与原版一致"，再讨论"原版哪里不合理"，这两件事分开
- **共用件要用「并列提供」而不是「改语义」**：`UVField` 与 SEAMT 共用（控制变量），复刻 MATLAB 的零填充时没有改 `sample()`，而是**另加 `sampleZeroFill()` + `zeroFillInvalid` 开关**（默认 false）。同理任何共用基础件要加能力，优先加新方法/新开关，别动老方法的语义
- **自动化测试的请求文件是「一次性地雷」**：`ncdf_seamt_auto.txt` 一旦存在，`ncdf_pi::Init()` 就会在插件加载期建面板跑测试。测试脚本会把它改名 `.done`，但手工跑/中断留下的残留会让**每次启动 OpenCPN 都走这条路径**。排查加载问题前先删它
- **诊断开关绝不能在热路径上做 I/O**。`kuroFlagInt()` 原先每次调用都 `fopen` 一遍 `ncdf_flags.txt`，而 `kuroAvgZero()` 在**每条候选段**上调用（实测 `candSegs=687829`）= 每轮 68.8 万次 fopen/fclose，8 线程并发踩爆 CRT 文件锁。**提取 49 秒就是这么来的**，改成缓存 2 s 后降到 0.55 秒（89×）。渲染那边早有「每 30 次渲染重读一次」的限流，加新开关时要记得同样限流
- **"8 线程没变快"不是"并行化没用"，先量 CPU 总量 vs 墙上时间**。CPU 涨了 2.4 倍而墙上不动 = 线程在互相踩（锁竞争），不是算力不够。用 `GetProcessTimes` 取所有线程 CPU 之和，一比就知道往哪修
- **性能问题别猜，加工作量计数**。本项目连猜两轮都没中（cache 局部性、计算量），最后靠「调用次数 + CPU/墙 比值」一轮定位。日志里 `[kuro][work]` 就是为此保留的
- **重构时新写的分派函数，两条分支都要跑过才算验证**。`sampleF` 的 else 分支写成了调自己（无限递归→栈溢出→闪退），因为「零速格填充」那轮 A/B 被中断、只跑了 `kuroZeroFill=1` 的分支，另一个分支一直没执行过
- **调试期"临时关掉某功能"的代码必须回退或标注 TODO**。`BuildLandLayers` 里「停用精层只建粗层」是 `db59067` 验证「陆地不正常」时留下的，忘了回退；后来掩膜配置一变（只剩一份）就**一个图层都不建**，症状是「陆地被涂成海底色」，离原因很远
- **Win32 上 `nc_open` 打开"每天每层一组"的 HDF5 文件会吃掉 GB 级内存**。`kuroshio_2024.nc`（366 天 × 30 层 × 7 变量 = 76863 个数据集、10980 个组）一次 `nc_open` 申请 1.3 GB，打穿 2 GB 地址空间并让**之后所有 `nc_open` 都 bad_alloc**。目录树必须**懒加载**，别在面板打开时挨个开 nc
- **`Freeze()` 之后必须有异常安全的 `Thaw()`**。`fillDirTree` 中途抛异常会让树永久冻结、`m_isTreeUpdating` 卡 true，后续所有树事件被挡掉
- **`ncdfLog` 的锁必须有超时 + 异常保护**。持锁期间出事（坏 `%s` 指针等）会让之后每次 `ncdfLog` 原地自旋，把局部失败放大成「整机冻结、日志停在半句」

## 动画功能（已停用）
- 流场动画**当前停用**。`ncdf_animate.cpp` 保留源码并在工程里编译（`m_animate` 成员要链接），但取数入口被 `#if 0` 关掉，`ncdf.cpp` 里 `m_animate.Init/ComputeDisplacementMap/Timer.Start` 已注释
- 要重新启用：把 `ncdfoverlayfactory.cpp` 里 `bool animateActive = false;` 后面的 `#if 0` 改成 `#if 1`，并恢复 `ncdf.cpp` 里被 `// [动画已停用]` 注释的三行
- **两套动画实现曾并存**（`ncdf_animate` / `ncdfanimate`，同一对专利的两套代码），`ncdfanimate.*` 与无人引用的 `ncdf_animate_shader.*` 已删除。若以后要 GL 加速版，从 git 历史找

## 已知待优化 / 已定位（2026-10-28）
- **平移/缩放卡顿：已定位为 OpenCPN 自身，不是本插件**。实测插件渲染 CPU 开销仅 `tileUV=0.12 sea=0.00 fill=0.00 land=0.00 ms/frame`；把 `ncdf_flags.txt` 的 `sea/land/fill` 全置 0（插件不画任何东西）后平移**依旧卡**。注：`glDrawArrays` 是异步的，CPU 计时测不到 GPU 开销，但插件不绘制仍卡说明问题不在插件
- **同一文件内切换颜色图层卡顿** —— 已定位并修复：`setData` 原先无条件 `clearCoefficients()` 四层（删纹理+置 `gridReady=false`）+ 作废陆地图层，现改为 `dataChanged` 参数，切图层传 `false` 跳过破坏性清理
- **局部图四周那圈海底色/陆地取舍** —— 搁置，用户回退了相关改动
- **陆地概率性被渲染成海底色** —— 搁置；诊断日志保留：补空白在无掩膜时运行会记
  `[landlayer] WARN: gap-fill running with NO land mask`

## 性能实测基线（全球 4320×2041，2026-10-28）
| 阶段 | 优化前 | 优化后 |
|---|---|---|
| `BuildLandLayer` | 4016 ms | ~500 ms |
| `texture build`（每层） | 469~703 ms | 187~313 ms |
| `prepareAllGrids` | 306 ms | 74~110 ms |
| `prepareCoeff` | 194~426 ms | 91~271 ms |
| **初次加载合计** | **≈4.7 s** | **≈1.1 s** |
| 渲染 CPU（每帧） | — | 0.12 ms |

手段：`parallelFor` 并行（行/列独立的循环）、`NcdfLandMask::landMaskRow` 增量下标+位段 popcount、
`ncdfLog` 常驻句柄、同范围不重建陆地图层。`landMaskRow` 有自检（与逐格 `landFracInBox` 逐格比对）。

### Wang2026 提取实测基线（`海流1.nc` 全球 2041×4320，2026-10-29）
| | 优化前 | 优化后 |
|---|---|---|
| `integrate+collect` | 48 752 ms | **539 ms** |
| 提取总耗时 | 48 789 ms | **547 ms** |
| CPU 总量（所有线程之和） | 124 546 ms | **4 375 ms** |
| 并行效率（8 线程） | x2.55 | **x8.12** |
| 结果 | nAxis=177 len=2043km mean=0.74 max=1.17 segs=5/5 | **完全相同** |

工作量计数改前改后逐项相同（`integSteps=4199611 candSegs=687829 samplePts=54051210 loopPairs=58257078`），
证明优化没有动任何计算。手段：`kuroFlagInt` 缓存 2 s（68.8 万次 fopen/fclose 是元凶）、
`kuroAvgZero` 提出循环、`avgSpeed` 双趟合一、`detectLoops` 平方距离、`UVField::window` 裁视野。
回归比对用「工作量计数 + 结果量」两个一起看，只有耗时变而结果变 = 改坏了。

## 参考文档
- `docs/交接文档-2026-10-29-卡死闪退与性能排查.md`：**最新**。本轮 6 个 bug 的根因与修法（目录树重复扫描、日志锁、`nc_open` 打穿地址空间、`sampleF` 自递归、陆地图层、`kuroFlagInt` 68.8 万次 fopen）、测试型/使用型开关、掩膜取舍、Wang2026 提取 89× 优化与实测基线、排查方法论
- `docs/交接文档-2026-10-29-两条主轴算法与自动化测试.md`：两条算法的现状与分界、自动化测试用法与机制、诊断开关、零速格填充的实现、待办
- `docs/交接文档-黑潮主轴与陆海掩膜.md`：黑潮主轴提取算法（Wang2026 流线约束最大流速法）与 GSHHG 陆海掩膜的完整设计、`.landmask.bin` 文件格式、构建部署流程、以及**已回退改动清单**（动手前先看，避免重蹈覆辙）
- `docs/交接文档-渲染性能优化与结构整理.md`：性能基线（回归对比用）、模块划分、本轮修掉的三个 bug、踩坑清单、诊断工具用法、遗留问题
- `docs/SEAMT急流核心提取-方法论与实现方案.md`：SEAMT（Udalov2026 最小 T 参数法）的完整方法论、**实施后修订（§12，含被实测推翻的参数）**、自动化测试用法与回归基线（§13）、踩坑汇总（§14）、**验证结果（§15，已成功）**。与 Wang2026 适用域不重叠（上游 vs 下游），**不要混用**
- `docs/Wang2026主轴移植验证-排查记录.md`：Wang2026 移植正确性验证（进行中）。含发布数据集结构、5 天比对结果与分段分析、**「输出是速度脊不是流线」的关键认知**、8 条假设的排查链、诊断开关（`kuroThreads`/`kuroCap`/`kuroDbg`/`kuroAvgZero`/`kuroAlpha1000`）

- koa-connect wrapper caused ctx leaks, so native rewrite is required