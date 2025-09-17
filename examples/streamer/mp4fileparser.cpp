#include "mp4fileparser.hpp"
#include <iostream>
#include <netinet/in.h>
#include <opencv2/opencv.hpp>

MP4FileParser::MP4FileParser(const std::string &filename, unsigned fps, bool loop)
    : filename(filename), fps(fps), loop(loop), cap(filename), frameIndex(0), sampleTime_us(0),
      frameSkip_(0), encoder(1280, 720, 30) {
	if (!cap.isOpened()) {
		throw std::runtime_error("无法打开视频文件: " + filename);
	}
	videoFps = cap.get(cv::CAP_PROP_FPS);
	frameCount = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
	assert(videoFps > 0);
	frameDuration_us = static_cast<uint64_t>(1000000.0 / videoFps);
	setInputCameraFPS(fps);
}

MP4FileParser::~MP4FileParser() { cap.release(); }

void MP4FileParser::start() {
	sampleTime_us = std::numeric_limits<uint64_t>::max() - frameDuration_us + 1;
	loadNextSample();
}

void MP4FileParser::stop() {
	sample = {};
	sampleTime_us = 0;
	frameIndex = -1;
}

void MP4FileParser::setInputCameraFPS(int cam_fps) {
	frameSkip_ = cam_fps / videoFps;
	if (frameSkip_ < 1)
		frameSkip_ = 1; // 防止低帧率摄像头出错
}

void MP4FileParser::loadNextSample() {
	cv::Mat frame;
	if (!cap.read(frame)) {
		if (loop) {
			cap.set(cv::CAP_PROP_POS_FRAMES, 0);
			frameIndex = 0;
			if (!cap.read(frame))
				return;
		} else {
			return;
		}
	}

	frameIndex++;

	// 跳过帧，不编码，读取下一帧
	// if (frameIndex % frameSkip_ != 0) {
	// 	loadNextSample();
	// 	return;
	// }

	static auto lastTime = std::chrono::steady_clock::now();
	static int frameCounter = 0;

	frameCounter++;
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime).count();
	if (elapsed >= 1) {
		std::cout << "处理速率FPS: " << frameCounter / elapsed << std::endl;
		frameCounter = 0;
		lastTime = now;
	}

	// 将Mat数据转换为字节数组（BGR格式）
	const auto encoded = encoder.encode(frame);
	if (encoded.empty()) {
		loadNextSample();
		return;
	}

	// for (int i = 0; i < 16 && i < encoded.size(); ++i)
	// 	printf("%02X ", encoded[i]);
	// printf("\n");

	sample.assign(reinterpret_cast<const rtc::byte *>(encoded.data()),
	              reinterpret_cast<const rtc::byte *>(encoded.data() + encoded.size()));
	sampleTime_us = frameIndex * frameDuration_us;
	parseNALUs(sample);
}

void MP4FileParser::parseNALUs(const std::vector<std::byte> &sample) {
	size_t i = 0;
	while (i + 4 <= sample.size()) {
		// 检查是否为 start code (Annex B)
		if (!(std::to_integer<unsigned char>(sample[i]) == 0x00 &&
		      std::to_integer<unsigned char>(sample[i + 1]) == 0x00 &&
		      std::to_integer<unsigned char>(sample[i + 2]) == 0x00 &&
		      std::to_integer<unsigned char>(sample[i + 3]) == 0x01)) {
			++i;
			continue;
		}

		size_t naluStartIndex = i + 4;
		size_t naluEndIndex = naluStartIndex;

		// 查找下一个 start code
		while (naluEndIndex + 4 <= sample.size()) {
			if (std::to_integer<unsigned char>(sample[naluEndIndex]) == 0x00 &&
			    std::to_integer<unsigned char>(sample[naluEndIndex + 1]) == 0x00 &&
			    std::to_integer<unsigned char>(sample[naluEndIndex + 2]) == 0x00 &&
			    std::to_integer<unsigned char>(sample[naluEndIndex + 3]) == 0x01) {
				break;
			}
			++naluEndIndex;
		}
		auto header = reinterpret_cast<rtc::NalUnitHeader *>(
		    const_cast<std::byte *>(sample.data() + naluStartIndex));
		auto type = header->unitType();
		switch (type) {
		case 7:
			previousUnitType7 = {sample.begin() + i, sample.begin() + naluEndIndex};
			break;
		case 8:
			previousUnitType8 = {sample.begin() + i, sample.begin() + naluEndIndex};
			break;
		case 5:
			previousUnitType5 = {sample.begin() + i, sample.begin() + naluEndIndex};
			break;
		}
		i = naluEndIndex;
	}
}

std::vector<std::byte> MP4FileParser::initialNALUS() {
	std::vector<std::byte> units{};
	if (previousUnitType7.has_value()) {
		auto nalu = previousUnitType7.value();
		units.insert(units.end(), nalu.begin(), nalu.end());
	}
	if (previousUnitType8.has_value()) {
		auto nalu = previousUnitType8.value();
		units.insert(units.end(), nalu.begin(), nalu.end());
	}
	if (previousUnitType5.has_value()) {
		auto nalu = previousUnitType5.value();
		units.insert(units.end(), nalu.begin(), nalu.end());
	}
	return units;
}
