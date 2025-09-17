#pragma once
#include <iostream>
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

/**
 * 硬件 H.264 编码器封装
 * 支持：
 * - 输入 cv::Mat (BGR)
 * - 硬件编码（NVENC / QSV / VideoToolbox）
 * - 高帧率摄像头自动降采样
 */
class H264Encoder {
public:
	H264Encoder(int width, int height, int fps, const std::string &codec_name = "h264_nvenc")
	    : width_(width), height_(height), fps_(fps), codec_name_(codec_name) {
		initEncoder();
	}

	~H264Encoder() {
		if (frame_)
			av_frame_free(&frame_);
		if (pkt_)
			av_packet_free(&pkt_);
		if (sws_ctx_)
			sws_freeContext(sws_ctx_);
		if (codec_ctx_)
			avcodec_free_context(&codec_ctx_);
	}

	/**
	 * 输入一帧 cv::Mat (BGR格式)
	 * 返回 H.264 NALU 数据
	 */
	std::vector<uint8_t> encode(const cv::Mat &mat) {

		if (mat.empty())
			throw std::runtime_error("输入图像为空");

		cv::Mat resized;
		if (mat.cols != width_ || mat.rows != height_) {
			cv::resize(mat, resized, cv::Size(width_, height_));
		} else {
			resized = mat;
		}

		// BGR -> YUV420P
		const uint8_t *src_slices[1] = {resized.data};
		int src_stride[1] = {static_cast<int>(resized.step[0])};
		sws_scale(sws_ctx_, src_slices, src_stride, 0, height_, frame_->data, frame_->linesize);

		frame_->pts = pts_++;
		avcodec_send_frame(codec_ctx_, frame_);

		std::vector<uint8_t> encoded_data;
		while (avcodec_receive_packet(codec_ctx_, pkt_) == 0) {
			encoded_data.assign(pkt_->data, pkt_->data + pkt_->size);
			// std::cout << "Encoded NALU size: " << pkt_->size << std::endl;
			av_packet_unref(pkt_);
		}
		return encoded_data;
	}

private:
	int width_, height_;
	int fps_;
	int frame_count_ = 0;
	int frame_skip_ = 1;
	int64_t pts_ = 0;

	std::string codec_name_;
	AVCodec *codec_ = nullptr;
	AVCodecContext *codec_ctx_ = nullptr;
	AVFrame *frame_ = nullptr;
	AVPacket *pkt_ = nullptr;
	SwsContext *sws_ctx_ = nullptr;

	void initEncoder() {
		avcodec_register_all();
		codec_ = avcodec_find_encoder_by_name(codec_name_.c_str());
		if (!codec_)
			throw std::runtime_error("找不到硬件编码器: " + codec_name_);

		codec_ctx_ = avcodec_alloc_context3(codec_);
		codec_ctx_->width = width_;
		codec_ctx_->height = height_;
		codec_ctx_->time_base = {1, fps_};
		codec_ctx_->framerate = {fps_, 1};
		codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
		codec_ctx_->gop_size = 30;
		codec_ctx_->max_b_frames = 0;

		if (avcodec_open2(codec_ctx_, codec_, NULL) < 0)
			throw std::runtime_error("无法打开编码器");

		// 分配帧
		frame_ = av_frame_alloc();
		frame_->format = codec_ctx_->pix_fmt;
		frame_->width = codec_ctx_->width;
		frame_->height = codec_ctx_->height;
		av_frame_get_buffer(frame_, 32);

		// 分配Packet
		pkt_ = av_packet_alloc();

		// BGR -> YUV420P
		sws_ctx_ = sws_getContext(width_, height_, AV_PIX_FMT_BGR24, width_, height_,
		                          AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
	}
};
