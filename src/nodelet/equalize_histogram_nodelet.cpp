/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2020, Ivan Tarifa.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Ivan Tarifa nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/**
 * @function EqualizeHist_Demo.cpp
 * @brief Demo code for equalizeHist function
 * @author OpenCV team
 */
/**
 * @function clahe.cpp
 * @brief Demo TApi code for clahe filter
 * @author OpenCV team
 */

#include <ros/ros.h>
#include "opencv_apps/nodelet.h"
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>
#include <cv_bridge/cv_bridge.h>

#include "opencv2/core/ocl.hpp"
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <dynamic_reconfigure/server.h>

#include "opencv_apps/EqualizeHistogramConfig.h"

namespace opencv_apps
{
class EqualizeHistogramNodelet : public opencv_apps::Nodelet
{
  const std::string OPENCV_WINDOW = "Equalize Histogram Window";
  image_transport::Publisher img_pub_;
  image_transport::Subscriber img_sub_;
  image_transport::CameraSubscriber cam_sub_;

  cv::Ptr<cv::CLAHE> clahe_;

  boost::shared_ptr<image_transport::ImageTransport> it_;

  typedef opencv_apps::EqualizeHistogramConfig Config;
  typedef dynamic_reconfigure::Server<Config> ReconfigureServer;

  Config config_;
  boost::shared_ptr<ReconfigureServer> reconfigure_server_;

  int queue_size_;
  bool debug_view_;
  bool use_opencl_;

  cv::Size clahe_tile_size_;
  double clahe_clip_limit_;

  void reconfigureCallback(Config& new_config, uint32_t level)
  {
    config_ = new_config;
    clahe_tile_size_ = cv::Size(config_.clahe_tile_size_x, config_.clahe_tile_size_y);
    clahe_clip_limit_ = config_.clahe_clip_limit;
  }

  void imageCallbackWithInfo(const sensor_msgs::ImageConstPtr& msg, const sensor_msgs::CameraInfoConstPtr& cam_info)
  {
    doWork(msg, cam_info->header.frame_id);
  }

  void imageCallback(const sensor_msgs::ImageConstPtr& msg)
  {
    doWork(msg, msg->header.frame_id);
  }

  void doWork(const sensor_msgs::ImageConstPtr& msg, const std::string& input_frame_from_msg)
  {
    try
    {
      // Convert the image into something opencv can handle.
      cv::UMat frame;
      if (msg->encoding == sensor_msgs::image_encodings::BGR8)
        frame = cv_bridge::toCvShare(msg, sensor_msgs::image_encodings::BGR8)->image.getUMat(cv::ACCESS_RW);
      else if (msg->encoding == sensor_msgs::image_encodings::MONO8)
        frame = cv_bridge::toCvShare(msg, sensor_msgs::image_encodings::MONO8)->image.getUMat(cv::ACCESS_RW);

      if (debug_view_)
      {
        cv::namedWindow(OPENCV_WINDOW, cv::WINDOW_AUTOSIZE);
      }

      // Do the work
      cv::UMat gray, dst;
      if (frame.channels() > 1)
      {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
      }
      else
      {
        frame.copyTo(gray);
      }

      switch (config_.histogram_equalization_type)
      {
        case opencv_apps::EqualizeHistogram_Clahe:
          if (clahe_ == nullptr)
            clahe_ = cv::createCLAHE();
          clahe_->setTilesGridSize(clahe_tile_size_);
          clahe_->setClipLimit(clahe_clip_limit_);
          clahe_->apply(gray, dst);
          break;
        case opencv_apps::EqualizeHistogram_EqualizeHist:
          equalizeHist(gray, dst);
          break;
      }

      //-- Show what you got
      if (debug_view_)
      {
        cv::imshow(OPENCV_WINDOW, dst);
        int c = cv::waitKey(1);
      }

      // Publish the image.
      sensor_msgs::Image::Ptr out_img =
          cv_bridge::CvImage(msg->header, sensor_msgs::image_encodings::MONO8, dst.getMat(cv::ACCESS_READ))
              .toImageMsg();
      out_img->header.frame_id = input_frame_from_msg;
      img_pub_.publish(out_img);
    }
    catch (cv::Exception& e)
    {
      NODELET_ERROR("Image processing error: %s %s %s %i", e.err.c_str(), e.func.c_str(), e.file.c_str(), e.line);
    }
  }

  void subscribe()  // NOLINT(modernize-use-override)
  {
    NODELET_DEBUG("Subscribing to image topic.");
    if (config_.use_camera_info)
      cam_sub_ = it_->subscribeCamera("image", queue_size_, &EqualizeHistogramNodelet::imageCallbackWithInfo, this);
    else
      img_sub_ = it_->subscribe("image", queue_size_, &EqualizeHistogramNodelet::imageCallback, this);
  }

  void unsubscribe()  // NOLINT(modernize-use-override)
  {
    NODELET_DEBUG("Unsubscribing from image topic.");
    img_sub_.shutdown();
    cam_sub_.shutdown();
  }

  void onInit()  // NOLINT(modernize-use-override)
  {
    Nodelet::onInit();
    it_ = boost::shared_ptr<image_transport::ImageTransport>(new image_transport::ImageTransport(*nh_));
    pnh_->param("queue_size", queue_size_, 3);
    pnh_->param("debug_view", debug_view_, false);
    pnh_->param("use_opencl", use_opencl_, true);

    reconfigure_server_ = boost::make_shared<dynamic_reconfigure::Server<Config> >(*pnh_);
    dynamic_reconfigure::Server<Config>::CallbackType f =
        boost::bind(&EqualizeHistogramNodelet::reconfigureCallback, this, _1, _2);
    reconfigure_server_->setCallback(f);

    img_pub_ = advertiseImage(*pnh_, "image", 1);

    cv::ocl::setUseOpenCL(use_opencl_);

    onInitPostProcess();
  }
};
}  // namespace opencv_apps

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(opencv_apps::EqualizeHistogramNodelet, nodelet::Nodelet);
