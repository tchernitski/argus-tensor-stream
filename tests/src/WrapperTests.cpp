#include <gtest/gtest.h>

#include "WrapperC.h"
extern "C" {
#include "libavutil/crc.h"
}

void getCycle(std::map<std::string, std::string> parameters, TensorStream& reader) {
	try {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());

		std::shared_ptr<FILE> dumpFile(std::shared_ptr<FILE>(fopen(parameters["dumpName"].c_str(), "ab"), std::fclose));
		ResizeOptions resizeOptions;
		resizeOptions.width = width;
		resizeOptions.height = height;
		ColorOptions colorOptions;
		colorOptions.dstFourCC = format;
		FrameParameters frameArgs = { resizeOptions, colorOptions };
		for (int i = 0; i < frames; i++) {
			auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
			int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
			if (status < 0)
				return;

		}
	}
	catch (std::runtime_error e) {
		return;
	}

}


void checkCRC(std::map<std::string, std::string> parameters, uint64_t crc) {
	int width = std::atoi(parameters["width"].c_str());
	int height = std::atoi(parameters["height"].c_str());
	int channels = 3;
	if ((FourCC)std::atoi(parameters["format"].c_str()) == Y800)
		channels = 1;

	int frames = std::atoi(parameters["frames"].c_str());
	std::vector<uint8_t> fileRGBProcessing(width * height * channels * frames);
	{
		std::shared_ptr<FILE> readFile(fopen(parameters["dumpName"].c_str(), "rb"), fclose);
		fread(&fileRGBProcessing[0], fileRGBProcessing.size(), 1, readFile.get());
	}
	if (av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, &fileRGBProcessing[0], width * height * channels * frames) != crc) {
		ASSERT_EQ(remove(parameters["dumpName"].c_str()), 0);
		ASSERT_EQ(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, &fileRGBProcessing[0], width * height * channels * frames), crc);
	}
	
	ASSERT_EQ(remove(parameters["dumpName"].c_str()), 0);
}

TEST(Wrapper_Init, OneThread) {
	TensorStream reader;
	reader.enableLogs(MEDIUM);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"}, 
													  {"frames", "10"}, {"dumpName", "bbb_dump.yuv"} };
	//Remove artifacts from previous runs
	remove(parameters["dumpName"].c_str());
	std::thread get(getCycle, parameters, std::ref(reader));
	get.join();
	reader.endProcessing();
	pipeline.join();
	//let's compare output

	checkCRC(parameters, 734055672);
}

//several threads
TEST(Wrapper_Init, MultipleThreads) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parametersFirst = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "10"}, {"dumpName", "bbb_dumpFirst.yuv"} };
	std::map<std::string, std::string> parametersSecond = { {"name", "second"}, {"delay", "-1"}, {"format", std::to_string(Y800)}, {"width", "1920"}, {"height", "1080"},
													  {"frames", "9"}, {"dumpName", "bbb_dumpSecond.yuv"} };
	//Remove artifacts from previous runs
	remove(parametersFirst["dumpName"].c_str());
	remove(parametersSecond["dumpName"].c_str());
	std::thread getFirst(getCycle, parametersFirst, std::ref(reader));
	std::thread getSecond(getCycle, parametersSecond, std::ref(reader));
	getFirst.join();
	getSecond.join();
	reader.endProcessing();
	pipeline.join();
	//let's compare output

	checkCRC(parametersFirst, 734055672);
	checkCRC(parametersSecond, 2107993070);

}

void getCycleLD(std::map<std::string, std::string> parameters, TensorStream& reader) {
	try {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());
		
		FrameParameters frameArgs = { ResizeOptions(width, height), ColorOptions(format) };
		for (int i = 0; i < frames; i++) {
			std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
			auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
			int sleepTime = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::high_resolution_clock::now() - startTime).count();
			//skip first several frames due to some possible additional time needed for decoded/parser to start processing
			if (i > 3) {
				ASSERT_GT(sleepTime, reader.getDelay() - 4);
				ASSERT_LT(sleepTime, reader.getDelay() + 4);
			}
		}
	}
	catch (std::runtime_error e) {
		return;
	}

}

//delay
TEST(Wrapper_Init, CheckPerformance) {
	TensorStream reader;
	reader.enableLogs(MEDIUM);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "10"} };

	std::thread getFirst(getCycleLD, parameters, std::ref(reader));
	getFirst.join();
	reader.endProcessing();
	pipeline.join();
}

//this test should be at the end
TEST(Wrapper_Init, OneThreadHang) {
	bool ended = false;
	std::thread mainThread([&ended]() {
		TensorStream reader;
		reader.enableLogs(MEDIUM);
		ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5), VREADER_OK);
		std::thread pipeline(&TensorStream::startProcessing, &reader);
		std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														  {"frames", "10"}, {"dumpName", "bbb_dump.yuv"} };
		//Remove artifacts from previous runs
		remove(parameters["dumpName"].c_str());
		std::thread get(getCycle, parameters, std::ref(reader));
		//wait for some processing happened
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		//Close Reader before joining any thread, expect no hangs at the end of program
		reader.endProcessing();
		get.join();
		reader.endProcessing();
		pipeline.join();
		//let's compare output
		ended = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	ASSERT_EQ(ended, true);
	mainThread.join();
}

TEST(Wrapper_Init, SeveralInstances) {
	//need to check logs levels and correctness of frames
	TensorStream readerBBB;
	//readerBBB.enableLogs(-LOW);
	ASSERT_EQ(readerBBB.initPipeline("../resources/bbb_1080x608_420_10.h264", 5), VREADER_OK);
	TensorStream readerBilliard;
	//readerBilliard.enableLogs(-MEDIUM);
	ASSERT_EQ(readerBilliard.initPipeline("../resources/billiard_1920x1080_420_100.h264", 5), VREADER_OK);
	std::thread pipelineBBB(&TensorStream::startProcessing, &readerBBB);
	std::thread pipelineBilliard(&TensorStream::startProcessing, &readerBilliard);
	std::map<std::string, std::string> parametersBBB = { {"name", "BBB"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "1920"}, {"height", "1080"},
													  {"frames", "10"}, {"dumpName", "BBB_dump.yuv"} };
	std::map<std::string, std::string> parametersBilliard = { {"name", "Billiard"}, {"delay", "0"}, {"format", std::to_string(BGR24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "10"}, {"dumpName", "billiard_dump.yuv"} };
	
	std::thread getBBB(getCycle, parametersBBB, std::ref(readerBBB));
	std::thread getBilliard(getCycle, parametersBilliard, std::ref(readerBilliard));
	
	getBBB.join();
	getBilliard.join();
	readerBBB.endProcessing();
	readerBilliard.endProcessing();
	pipelineBBB.join();
	pipelineBilliard.join();
	//let's compare output

	checkCRC(parametersBBB, 3267473238);
	checkCRC(parametersBilliard, 3378171067);
}