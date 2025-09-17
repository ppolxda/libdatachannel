#pragma once
#include "h264encoder.hpp"
#include "stream.hpp"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class MP4FileParser : public StreamSource {
public:
	MP4FileParser(const std::string &filename, unsigned fps, bool loop = true);
	~MP4FileParser();

	void start() override;
	void stop() override;
	void loadNextSample() override;

	rtc::binary getSample() override { return sample; };
	uint64_t getSampleTime_us() override { return sampleTime_us; };
	uint64_t getSampleDuration_us() override { return frameDuration_us; };
	std::vector<std::byte> initialNALUS();
	void parseNALUs(const std::vector<std::byte> &sample);

private:
	void setInputCameraFPS(int cam_fps);

protected:
	rtc::binary sample = {};
	uint64_t sampleTime_us = 0;

private:
	std::string filename;
	unsigned fps;
	unsigned frameSkip_;
	bool loop;

	cv::VideoCapture cap;
	unsigned frameIndex;
	int frameCount;
	double videoFps;
	uint64_t frameDuration_us;
	H264Encoder encoder;

	// 收集并返回 H.264 码流的关键初始化 NALU
	std::optional<std::vector<std::byte>> previousUnitType5 = std::nullopt;
	std::optional<std::vector<std::byte>> previousUnitType7 = std::nullopt;
	std::optional<std::vector<std::byte>> previousUnitType8 = std::nullopt;
};
