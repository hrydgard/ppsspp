cmake_minimum_required(VERSION 3.10.0)
project(oboe)

# Our own build of Oboe rather than ext/oboe/CMakeLists.txt, which needs a newer CMake
# and turns on -Werror for Debug builds.
set(SRC_DIR ../oboe)

set(ALL_SOURCE_FILES
	${SRC_DIR}/src/aaudio/AAudioLoader.cpp
	${SRC_DIR}/src/aaudio/AudioStreamAAudio.cpp
	${SRC_DIR}/src/common/AdpfWrapper.cpp
	${SRC_DIR}/src/common/AudioSourceCaller.cpp
	${SRC_DIR}/src/common/AudioStream.cpp
	${SRC_DIR}/src/common/AudioStreamBuilder.cpp
	${SRC_DIR}/src/common/DataConversionFlowGraph.cpp
	${SRC_DIR}/src/common/FilterAudioStream.cpp
	${SRC_DIR}/src/common/FixedBlockAdapter.cpp
	${SRC_DIR}/src/common/FixedBlockReader.cpp
	${SRC_DIR}/src/common/FixedBlockWriter.cpp
	${SRC_DIR}/src/common/LatencyTuner.cpp
	${SRC_DIR}/src/common/OboeExtensions.cpp
	${SRC_DIR}/src/common/QuirksManager.cpp
	${SRC_DIR}/src/common/SourceFloatCaller.cpp
	${SRC_DIR}/src/common/SourceI16Caller.cpp
	${SRC_DIR}/src/common/SourceI24Caller.cpp
	${SRC_DIR}/src/common/SourceI32Caller.cpp
	${SRC_DIR}/src/common/StabilizedCallback.cpp
	${SRC_DIR}/src/common/Trace.cpp
	${SRC_DIR}/src/common/Utilities.cpp
	${SRC_DIR}/src/common/Version.cpp
	${SRC_DIR}/src/fifo/FifoBuffer.cpp
	${SRC_DIR}/src/fifo/FifoController.cpp
	${SRC_DIR}/src/fifo/FifoControllerBase.cpp
	${SRC_DIR}/src/fifo/FifoControllerIndirect.cpp
	${SRC_DIR}/src/flowgraph/ChannelCountConverter.cpp
	${SRC_DIR}/src/flowgraph/ClipToRange.cpp
	${SRC_DIR}/src/flowgraph/FlowGraphNode.cpp
	${SRC_DIR}/src/flowgraph/Limiter.cpp
	${SRC_DIR}/src/flowgraph/ManyToMultiConverter.cpp
	${SRC_DIR}/src/flowgraph/MonoBlend.cpp
	${SRC_DIR}/src/flowgraph/MonoToMultiConverter.cpp
	${SRC_DIR}/src/flowgraph/MultiToManyConverter.cpp
	${SRC_DIR}/src/flowgraph/MultiToMonoConverter.cpp
	${SRC_DIR}/src/flowgraph/RampLinear.cpp
	${SRC_DIR}/src/flowgraph/SampleRateConverter.cpp
	${SRC_DIR}/src/flowgraph/SinkFloat.cpp
	${SRC_DIR}/src/flowgraph/SinkI16.cpp
	${SRC_DIR}/src/flowgraph/SinkI24.cpp
	${SRC_DIR}/src/flowgraph/SinkI32.cpp
	${SRC_DIR}/src/flowgraph/SinkI8_24.cpp
	${SRC_DIR}/src/flowgraph/SourceFloat.cpp
	${SRC_DIR}/src/flowgraph/SourceI16.cpp
	${SRC_DIR}/src/flowgraph/SourceI24.cpp
	${SRC_DIR}/src/flowgraph/SourceI32.cpp
	${SRC_DIR}/src/flowgraph/SourceI8_24.cpp
	${SRC_DIR}/src/flowgraph/resampler/IntegerRatio.cpp
	${SRC_DIR}/src/flowgraph/resampler/LinearResampler.cpp
	${SRC_DIR}/src/flowgraph/resampler/MultiChannelResampler.cpp
	${SRC_DIR}/src/flowgraph/resampler/PolyphaseResampler.cpp
	${SRC_DIR}/src/flowgraph/resampler/PolyphaseResamplerMono.cpp
	${SRC_DIR}/src/flowgraph/resampler/PolyphaseResamplerStereo.cpp
	${SRC_DIR}/src/flowgraph/resampler/SincResampler.cpp
	${SRC_DIR}/src/flowgraph/resampler/SincResamplerStereo.cpp
	${SRC_DIR}/src/opensles/AudioInputStreamOpenSLES.cpp
	${SRC_DIR}/src/opensles/AudioOutputStreamOpenSLES.cpp
	${SRC_DIR}/src/opensles/AudioStreamBuffered.cpp
	${SRC_DIR}/src/opensles/AudioStreamOpenSLES.cpp
	${SRC_DIR}/src/opensles/EngineOpenSLES.cpp
	${SRC_DIR}/src/opensles/OpenSLESUtilities.cpp
	${SRC_DIR}/src/opensles/OutputMixerOpenSLES.cpp
)

add_library(oboe STATIC ${ALL_SOURCE_FILES})
target_include_directories(oboe PRIVATE ${SRC_DIR}/src PUBLIC ${SRC_DIR}/include)
target_link_libraries(oboe PRIVATE log OpenSLES)
