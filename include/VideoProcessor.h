#pragma once

extern "C" {
	#include <libavformat/avformat.h>
}

#include <memory>
#include <vector>
#include <cuda_runtime.h>
#include <mutex>

/** @addtogroup cppAPI
@{
*/

/** Supported frame output color formats
 @details Used in @ref TensorStream::getFrame() function
*/
enum FourCC {
	Y800 = 0, /**< Monochrome format, 8 bit for pixel */
	RGB24, /**< RGB format, 24 bit for pixel, color plane order: R, G, B */
	BGR24 /**< RGB format, 24 bit for pixel, color plane order: B, G, R */
};

/** Possible planes order in RGB format
*/
enum Planes {
	PLANAR = 0, /**< Color components R, G, B are stored in memory separately like RRRRR, GGGGG, BBBBB*/
	MERGED /**< Color components R, G, B are stored in memory one by one like RGBRGBRGB */
};

/** Parameters specific for color conversion
*/
struct ColorOptions {
	bool normalization = false; /**<  @anchor normalization Should final colors be normalized or not */
	Planes planesPos = Planes::MERGED /**< Memory layout of pixels. See @ref ::Planes for more information */;
	FourCC dstFourCC = FourCC::RGB24 /**< Desired destination FourCC. See @ref ::FourCC for more information */;
};

/** Algorithm used to do resize
*/
enum ResizeType {
	NEAREST = 0, /**< Simple algorithm without any interpolation */
	BILINEAR /** Algorithm that does simple linear interpolation */
};

/** Parameters specific for resize
*/
struct ResizeOptions {
	unsigned int width = 0; /**< Width of destination image */
	unsigned int height = 0; /**< Height of destination image */
	ResizeType type = ResizeType::NEAREST; /**< Resize algorithm. See @ref ::ResizeType for more information */
};

/** Parameters used to configure VPP
 @details These parameters can be passed via @ref TensorStream::getFrame() function
*/
struct FrameParameters {
	ResizeOptions resize; /**< Resize options, see @ref ::ResizeOptions for more information */
	ColorOptions color; /**< Color conversion options, see @ref ::ColorParameters for more information*/
};

/**
@}
*/
template <class T>
int colorConversionKernel(AVFrame* src, AVFrame* dst, ColorOptions color, int maxThreadsPerBlock, cudaStream_t* stream);

int resizeKernel(AVFrame* src, AVFrame* dst, ResizeType resize, int maxThreadsPerBlock, cudaStream_t * stream);

class VideoProcessor {
public:
	int Init(bool _enableDumps = false);
	/*
	Check if VPP conversion for input package is needed and perform conversion.
	Notice: VPP doesn't allocate memory for output frame, so correctly allocated Tensor with correct FourCC and resolution
	should be passed via Python API	and this allocated CUDA memory will be filled.
	*/
	int Convert(AVFrame* input, AVFrame* output, FrameParameters options, std::string consumerName);
	template <class T>
	int DumpFrame(T* output, FrameParameters options, std::shared_ptr<FILE> dumpFile);
	void Close();
private:
	bool enableDumps;
	cudaDeviceProp prop;
	//own stream for every consumer
	std::vector<std::pair<std::string, cudaStream_t> > streamArr;
	std::mutex streamSync;
	//own dump file for every consumer
	std::vector<std::pair<std::string, std::shared_ptr<FILE> > > dumpArr;
	std::mutex dumpSync;
	/*
	State of component
	*/
	bool isClosed = true;
};