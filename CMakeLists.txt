cmake_minimum_required(VERSION 3.16)

# vcpkg 工具链 (必须在 project() 之前设置)
if(EXISTS "F:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "F:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# 项目名称
project(read LANGUAGES CXX)

# 设置 C++ 标准 (LibTorch 通常需要 C++14 或 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------
# 1. 寻找依赖库
# ---------------------------------------------------------

# (A) Qt5 (根据你的日志，你用的是 Qt5)
# 你使用的是 msvc2017_64 版本，这在 VS2022 下通常是兼容的
list(APPEND CMAKE_PREFIX_PATH "D:/Qt/Qt5.14.2/5.14.2/msvc2017_64")
find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)

# (B) LibTorch
# 提示 CMake 去哪里找 Torch (填你的解压路径)
list(APPEND CMAKE_PREFIX_PATH "F:/Ku/libtorch") 
find_package(Torch REQUIRED)

# (C) VTK — 强制使用本地编译的 VTK 8.2，不用 vcpkg 的 9.3
set(VTK_DIR "F:/Ku/VTK/Release/lib/cmake/vtk-8.2" CACHE PATH "" FORCE)
find_package(VTK REQUIRED PATHS "F:/Ku/VTK/Release/lib/cmake/vtk-8.2" NO_DEFAULT_PATH)

# (D) ITK
#list(APPEND CMAKE_PREFIX_PATH "D:/itk-5.2/release/lib/cmake/ITK-5.2")
#find_package(ITK REQUIRED)

# (E) DCMTK
# DCMTK 有时候比较麻烦，如果安装在标准路径通常能找到
# find_package(DCMTK REQUIRED)

# (F) Armadillo + SuperLU (FEM 求解)
find_package(Armadillo CONFIG QUIET)
find_package(superlu CONFIG QUIET)

# (G) CGAL (四面体网格生成)
find_package(CGAL CONFIG QUIET)

# (H) OpenMP (并行加速)
find_package(OpenMP QUIET)

# ---------------------------------------------------------
# 2. 添加源文件
# ---------------------------------------------------------

# 自动查找 src 目录下的 cpp 和 include 目录下的 h
# 注意加入了 CONFIGURE_DEPENDS
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/*.h")

# 处理 Qt 的 MOC (元对象编译器)
# 如果你的头文件里有 Q_OBJECT 宏，这步是必须的
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------
# 3. 生成可执行文件
# ---------------------------------------------------------

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 添加头文件搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Torch_INCLUDE_DIRS}
    ${VTK_INCLUDE_DIRS}
    #${ITK_INCLUDE_DIRS}
    #${DCMTK_INCLUDE_DIRS}
)

# FEM 依赖的条件编译宏
if(ARMADILLO_FOUND OR TARGET armadillo)
  target_compile_definitions(${PROJECT_NAME} PRIVATE DBS_HAS_ARMADILLO)
endif()
if(CGAL_FOUND OR TARGET CGAL::CGAL)
  target_compile_definitions(${PROJECT_NAME} PRIVATE DBS_HAS_CGAL)
endif()

# ---------------------------------------------------------
# 4. 链接库 (核心步骤)
# ---------------------------------------------------------

# CMake 会自动根据当前的构建模式 (Debug/Release) 
# 选择对应的库文件 (例如 vtkCommonCore.lib 或 vtkCommonCored.lib)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    ${TORCH_LIBRARIES}
    ${VTK_LIBRARIES}
    #${ITK_LIBRARIES}
    #${DCMTK_LIBRARIES}
)

# FEM 可选依赖
if(ARMADILLO_FOUND OR TARGET armadillo)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${ARMADILLO_LIBRARIES})
endif()
if(TARGET superlu::superlu)
  find_package(metis CONFIG QUIET)
  target_link_libraries(${PROJECT_NAME} PRIVATE superlu::superlu)
  if(TARGET metis)
    target_link_libraries(${PROJECT_NAME} PRIVATE metis)
  endif()
endif()
if(CGAL_FOUND OR TARGET CGAL::CGAL)
  target_link_libraries(${PROJECT_NAME} PRIVATE CGAL::CGAL)
endif()
if(OpenMP_CXX_FOUND)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
endif()

# 解决 LibTorch 和其他库可能存在的 flag 冲突
if(MSVC)
  # /W3: 警告等级3
  # /utf-8: 让 MSVC 将源文件和执行字符集都识别为 UTF-8
  #         这样 UTF-8 编码的中文注释不会报错，IDE 也能正常显示
  target_compile_options(${PROJECT_NAME} PRIVATE "/W3" "/utf-8")
endif()