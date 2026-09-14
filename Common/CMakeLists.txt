if(WIN32)
	set(CommonD3D
		GPU/D3D11/thin3d_d3d11.cpp
		GPU/D3D11/D3D11Loader.cpp
		GPU/D3D11/D3D11Loader.h
	)
endif()

set(CommonVR
	VR/OpenXRLoader.cpp
	VR/OpenXRLoader.h
	VR/PPSSPPVR.cpp
	VR/PPSSPPVR.h
	VR/VRBase.cpp
	VR/VRBase.h
	VR/VRFramebuffer.cpp
	VR/VRFramebuffer.h
	VR/VRInput.cpp
	VR/VRInput.h
	VR/VRMath.cpp
	VR/VRMath.h
	VR/VRRenderer.cpp
	VR/VRRenderer.h
)

add_library(Common STATIC
	ABI.cpp
	ABI.h
	CPUDetect.cpp
	CPUDetect.h
	Thunk.cpp
	Thunk.h
	x64Analyzer.cpp
	x64Analyzer.h
	x64Emitter.cpp
	x64Emitter.h
	ArmCPUDetect.cpp
	ArmEmitter.h
	ArmEmitter.cpp
	Arm64Emitter.h
	Arm64Emitter.cpp
	ArmEmitter.h
	ArmEmitter.cpp
	RiscVCPUDetect.cpp
	RiscVEmitter.cpp
	RiscVEmitter.h
	LoongArchCPUDetect.cpp
	LoongArch64Emitter.cpp
	LoongArch64Emitter.h
	${CommonD3D}
	${CommonVR}
	Serialize/Serializer.cpp
	Serialize/Serializer.h
	Serialize/SerializeDeque.h
	Serialize/SerializeFuncs.h
	Serialize/SerializeList.h
	Serialize/SerializeMap.h
	Serialize/SerializeSet.h
	Crypto/md5.cpp
	Crypto/md5.h
	Crypto/sha1.cpp
	Crypto/sha1.h
	Crypto/sha256.cpp
	Crypto/sha256.h
	Data/Collections/ConstMap.h
	Data/Collections/FixedSizeQueue.h
	Data/Collections/Hashmaps.h
	Data/Collections/TinySet.h
	Data/Collections/FastVec.h
	Data/Collections/CharQueue.h
	Data/Collections/CyclicBuffer.h
	Data/Collections/ThreadSafeList.h
	Data/Color/RGBAUtil.cpp
	Data/Color/RGBAUtil.h
	Data/Convert/ColorConv.cpp
	Data/Convert/ColorConv.h
	Data/Convert/SmallDataConvert.cpp
	Data/Convert/SmallDataConvert.h
	Data/Encoding/Base64.cpp
	Data/Encoding/Base64.h
	Data/Encoding/Compression.cpp
	Data/Encoding/Compression.h
	Data/Encoding/Shiftjis.h
	Data/Encoding/Utf8.cpp
	Data/Encoding/Utf8.h
	Data/Encoding/Utf16.h
	Data/Format/RIFF.cpp
	Data/Format/RIFF.h
	Data/Format/IniFile.cpp
	Data/Format/IniFile.h
	Data/Format/JSONReader.h
	Data/Format/JSONReader.cpp
	Data/Format/JSONWriter.h
	Data/Format/JSONWriter.cpp
	Data/Format/DDSLoad.cpp
	Data/Format/DDSLoad.h
	Data/Format/PNGLoad.cpp
	Data/Format/PNGLoad.h
	Data/Format/ZIMLoad.cpp
	Data/Format/ZIMLoad.h
	Data/Format/ZIMSave.cpp
	Data/Format/ZIMSave.h
	Data/Hash/Hash.cpp
	Data/Hash/Hash.h
	Data/Text/Demangle.cpp
	Data/Text/Demangle.h
	Data/Text/I18n.cpp
	Data/Text/I18n.h
	Data/Text/Parsers.cpp
	Data/Text/Parsers.h
	Data/Text/StringWriter.h
	Data/Text/WrapText.cpp
	Data/Text/WrapText.h
	Data/Random/Rng.h
	File/VFS/VFS.h
	File/VFS/VFS.cpp
	File/VFS/ZipFileReader.cpp
	File/VFS/ZipFileReader.h
	File/VFS/SevenZipFileReader.cpp
	File/VFS/SevenZipFileReader.h
	File/VFS/DirectoryReader.cpp
	File/VFS/DirectoryReader.h
	File/AndroidStorage.h
	File/AndroidStorage.cpp
	File/AndroidContentURI.h
	File/AndroidContentURI.cpp
	File/DiskFree.h
	File/DiskFree.cpp
	File/Path.h
	File/Path.cpp
	File/PathBrowser.h
	File/PathBrowser.cpp
	File/FileUtil.cpp
	File/FileUtil.h
	File/DirListing.cpp
	File/DirListing.h
	File/FileDescriptor.cpp
	File/FileDescriptor.h
	GPU/GraphicsContext.h
	GPU/DataFormat.h
	GPU/MiscTypes.h
	GPU/GPUBackendCommon.cpp
	GPU/GPUBackendCommon.h
	GPU/thin3d.cpp
	GPU/thin3d.h
	GPU/thin3d_create.h
	GPU/Shader.cpp
	GPU/Shader.h
	GPU/ShaderWriter.cpp
	GPU/ShaderWriter.h
	GPU/ShaderTranslation.h
	GPU/ShaderTranslation.cpp
	GPU/OpenGL/GLCommon.h
	GPU/OpenGL/GLDebugLog.cpp
	GPU/OpenGL/GLDebugLog.h
	GPU/OpenGL/GLSLProgram.cpp
	GPU/OpenGL/GLSLProgram.h
	GPU/OpenGL/gl3stub.c
	GPU/OpenGL/gl3stub.h
	GPU/OpenGL/GLFeatures.cpp
	GPU/OpenGL/GLFeatures.h
	GPU/OpenGL/GLFrameData.cpp
	GPU/OpenGL/GLFrameData.h
	GPU/OpenGL/thin3d_gl.cpp
	GPU/OpenGL/GLMemory.cpp
	GPU/OpenGL/GLMemory.h
	GPU/OpenGL/GLRenderManager.cpp
	GPU/OpenGL/GLRenderManager.h
	GPU/OpenGL/GLQueueRunner.cpp
	GPU/OpenGL/GLQueueRunner.h
	GPU/OpenGL/GLProfiler.cpp
	GPU/OpenGL/GLProfiler.h
	GPU/OpenGL/DataFormatGL.cpp
	GPU/OpenGL/DataFormatGL.h
	GPU/OpenGL/OpenGLGraphicsContext.cpp
	GPU/OpenGL/OpenGLGraphicsContext.h
	GPU/Vulkan/VulkanBarrier.cpp
	GPU/Vulkan/VulkanBarrier.h
	GPU/Vulkan/VulkanDebug.cpp
	GPU/Vulkan/VulkanDebug.h
	GPU/Vulkan/VulkanContext.cpp
	GPU/Vulkan/VulkanContext.h
	GPU/Vulkan/VulkanDescSet.cpp
	GPU/Vulkan/VulkanDescSet.h
	GPU/Vulkan/VulkanFramebuffer.cpp
	GPU/Vulkan/VulkanFramebuffer.h
	GPU/Vulkan/VulkanImage.cpp
	GPU/Vulkan/VulkanImage.h
	GPU/Vulkan/VulkanLoader.cpp
	GPU/Vulkan/VulkanLoader.h
	GPU/Vulkan/VulkanMemory.cpp
	GPU/Vulkan/VulkanMemory.h
	GPU/Vulkan/VulkanProfiler.cpp
	GPU/Vulkan/VulkanProfiler.h
	GPU/Vulkan/thin3d_vulkan.cpp
	GPU/Vulkan/VulkanRenderManager.cpp
	GPU/Vulkan/VulkanRenderManager.h
	GPU/Vulkan/VulkanQueueRunner.cpp
	GPU/Vulkan/VulkanQueueRunner.h
	GPU/Vulkan/VulkanFrameData.cpp
	GPU/Vulkan/VulkanFrameData.h
	GPU/Vulkan/VulkanPresentation.h
	GPU/Vulkan/VulkanGraphicsContext.cpp
	Input/GestureDetector.cpp
	Input/GestureDetector.h
	Input/KeyCodes.h
	Input/InputState.cpp
	Input/InputState.h
	Math/SIMDHeaders.h
	Math/SIMDHeaders.h
	Math/curves.cpp
	Math/curves.h
	Math/expression_parser.cpp
	Math/expression_parser.h
	Math/lin/matrix4x4.cpp
	Math/lin/matrix4x4.h
	Math/lin/vec3.cpp
	Math/lin/vec3.h
	Math/math_util.cpp
	Math/math_util.h
	Math/Statistics.h
	Math/Statistics.cpp
	Net/HTTPClient.cpp
	Net/HTTPClient.h
	Net/HTTPHeaders.cpp
	Net/HTTPHeaders.h
	Net/HTTPNaettRequest.cpp
	Net/HTTPNaettRequest.h
	Net/HTTPRequest.cpp
	Net/HTTPRequest.h
	Net/HTTPServer.cpp
	Net/HTTPServer.h
	Net/NetBuffer.cpp
	Net/NetBuffer.h
	Net/Resolve.cpp
	Net/Resolve.h
	Net/Sinks.cpp
	Net/Sinks.h
	Net/SocketCompat.h
	Net/URL.cpp
	Net/URL.h
	Net/WebsocketServer.cpp
	Net/WebsocketServer.h
	Profiler/Profiler.cpp
	Profiler/Profiler.h
	Render/AtlasGen.cpp
	Render/AtlasGen.h
	Render/TextureAtlas.cpp
	Render/TextureAtlas.h
	Render/DrawBuffer.cpp
	Render/DrawBuffer.h
	Render/ManagedTexture.cpp
	Render/ManagedTexture.h
	Render/Text/draw_text.cpp
	Render/Text/draw_text.h
	Render/Text/draw_text_android.cpp
	Render/Text/draw_text_android.h
	Render/Text/draw_text_win.cpp
	Render/Text/draw_text_win.h
	Render/Text/draw_text_uwp.cpp
	Render/Text/draw_text_uwp.h
	Audio/AudioBackend.h
	System/Display.cpp
	System/Display.h
	System/Application.h
	System/System.h
	System/NativeApp.h
	System/Request.cpp
	System/Request.h
	System/OSD.cpp
	System/OSD.h
	Thread/Channel.h
	Thread/ParallelLoop.cpp
	Thread/ParallelLoop.h
	Thread/Promise.h
	Thread/ThreadUtil.cpp
	Thread/ThreadUtil.h
	Thread/ThreadManager.cpp
	Thread/ThreadManager.h
	UI/AsyncImageFileView.cpp
	UI/AsyncImageFileView.h
	UI/Root.cpp
	UI/Root.h
	UI/Screen.cpp
	UI/Screen.h
	UI/ScreenManager.cpp
	UI/ScreenManager.h
	UI/UI.cpp
	UI/UI.h
	UI/Context.cpp
	UI/Context.h
	UI/IconCache.cpp
	UI/IconCache.h
	UI/UIScreen.cpp
	UI/UIScreen.h
	UI/Notice.cpp
	UI/Notice.h
	UI/Tween.cpp
	UI/Tween.h
	UI/View.cpp
	UI/View.h
	UI/ViewGroup.cpp
	UI/ViewGroup.h
	UI/ScrollView.cpp
	UI/ScrollView.h
	UI/TabHolder.cpp
	UI/TabHolder.h
	UI/PopupScreens.cpp
	UI/PopupScreens.h
	BitScan.h
	BitSet.h
	Buffer.h
	Buffer.cpp
	CodeBlock.h
	Common.h
	CommonFuncs.h
	CommonTypes.h
	DbgNew.h
	FakeEmitter.h
	FakeCPUDetect.cpp
	ExceptionHandlerSetup.cpp
	ExceptionHandlerSetup.h
	GhidraClient.h
	GhidraClient.cpp
	Log.h
	Log.cpp
	Log/ConsoleListener.cpp
	Log/ConsoleListener.h
	Log/LogManager.cpp
	Log/LogManager.h
	LogReporting.cpp
	LogReporting.h
	MemArenaAndroid.cpp
	MemArenaDarwin.cpp
	MemArenaPosix.cpp
	MemArenaWin32.cpp
	MemArenaHorizon.cpp
	MemArena.h
	MemoryUtil.cpp
	MemoryUtilHorizon.cpp
	MemoryUtil.h
	OSVersion.cpp
	OSVersion.h
	StringUtils.cpp
	StringUtils.h
	SysError.h
	SysError.cpp
	TimeUtil.cpp
	TimeUtil.h
	${CMAKE_SOURCE_DIR}/ext/jpge/jpgd.cpp
	${CMAKE_SOURCE_DIR}/ext/jpge/jpgd.h
	${CMAKE_SOURCE_DIR}/ext/jpge/jpge.cpp
	${CMAKE_SOURCE_DIR}/ext/jpge/jpge.h
)

if(LIBRETRO)
	target_include_directories(Common PRIVATE ${CMAKE_SOURCE_DIR}/libretro/libretro-common/include)
endif()

setup_target_project(Common Common)

if(IOS)
	target_compile_definitions(Common PUBLIC GLES_SILENCE_DEPRECATION)
endif()

target_link_libraries(Common Ext::Snappy cpu_features)

if(SDL_LIB_TARGET AND TARGET SDL3::Headers)
	target_link_libraries(Common SDL3::Headers)
endif()

if(NOT LIBRETRO)
	target_link_libraries(Common imgui)
endif()

if(ARM64)
	if(ANDROID)
		target_link_libraries(Common adrenotools)
	endif()
endif()

target_compile_definitions(Common PRIVATE Z7_CRC_NUM_TABLES=1)

if(TARGET Ext::GLEW)
	target_link_libraries(Common Ext::GLEW)
endif()

if(WIN32)
	target_link_libraries(Common winmm dsound dxguid d2d1 dwrite d3d11 Version)
endif()

target_link_libraries(Common ${LIBZIP_LIBRARY} 7zip ${PNG_LIBRARIES} basis_universal ${ZLIB_LIBRARY} pugixml vma gason udis86 ${RT_LIB} ${ATOMIC_LIB})

# ZIMLoad.cpp/ZIMSave.cpp, Serializer.cpp, and Render/TextureAtlas.cpp call directly into zstd.
if(USE_SYSTEM_ZSTD)
	target_include_directories(Common PRIVATE ${ZSTD_INCLUDE_DIR})
	target_link_libraries(Common ${ZSTD_LIBRARY})
else()
	target_link_libraries(Common libzstd_static)
endif()

# Common/GPU/ShaderTranslation.cpp and Common/GPU/Vulkan/VulkanContext.cpp call directly into
# glslang/spirv-cross, so Common needs to declare this dependency itself rather than relying on
# some downstream consumer happening to also link it (that was the previous, fragile setup: this
# was only linked to the old combined "core" target, which happened to always be linked after
# Common everywhere it mattered).
target_link_libraries(Common ${GlslangLibs})
