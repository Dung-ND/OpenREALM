/**
* This file is part of OpenREALM.
*
* Copyright (C) 2018 Alexander Kern <laxnpander at gmail dot com> (Braunschweig University of Technology)
* For more information see <https://github.com/laxnpander/OpenREALM>
*
* OpenREALM is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* OpenREALM is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with OpenREALM. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PROJECT_DENSIFICATION_STAGE_H
#define PROJECT_DENSIFICATION_STAGE_H

#include <deque>
#include <map>
#include <memory>
#include <chrono>

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/photo.hpp>

#include <realm_stages/stage_base.h>
#include <realm_stages/stage_settings.h>
#include <realm_stages/conversions.h>
#include <realm_io/cv_export.h>
#include <realm_core/stereo.h>
#include <realm_core/cv_grid_map.h>
#include <realm_core/frame.h>
#include <realm_densifier_base/densifier_factory.h>
#include <realm_densifier_base/sparse_depth.h>

namespace realm
{
namespace stages
{

class Densification : public StageBase
{
  public:
    using Ptr = std::shared_ptr<Densification>;
    using ConstPtr = std::shared_ptr<const Densification>;
  public:

    struct SaveSettings
    {
        bool save_bilat;
        bool save_dense;
        bool save_guided;
        bool save_imgs;
        bool save_sparse;
        bool save_thumb;
        bool save_normals;
    };

  public:
    /*!
     * @brief Constructor for densification stage. Instantiates a surface reconstruction framework as well. Provided
     *        settings are for stage and method seperated.
     * @param stage_set Stage settings for general processing of the densification
     * @param densifier_set Densifier framework settings
     */
    Densification(const StageSettings::Ptr &stage_set, const DensifierSettings::Ptr &densifier_set, double rate);

    /*!
     * @brief Function to add frames to the densification stage. Will be moved to the specific buffer depending on the
     *        settings.
     *        - In case of a 3d surface reconstruction they are moved to "_buffer_reco"
     *        - In case of no surface reconstruction they are moved to "_buffer_no_reco"
     *        Note that in case of a multi UAV operation mode frames for different UAVs are treated seperately
     * @param frame Input frame to be densified
     */
    void addFrame(const Frame::Ptr &frame) override;

    /*!
     * @brief Overriden function for processing. Is looped as part of the densification thread and will process,
     *        if new data can be processed inside the buffers.
     * @return True if data was processed
     */
    bool process() override;
  private:

    //! Flag for use of bilateral depth filtering
    bool _use_filter_bilat;

    //! Flag for use of guided depth filtering (currently not implemented)
    bool _use_filter_guided;

    //! Flag for surface normal computation
    bool _compute_normals;

    //! Files and data to be saved in the stage
    SaveSettings _settings_save;

    //! Number of frames used for stereo reconstruction
    uint8_t  _n_frames;

    //! Number of frames received for densification
    uint64_t _rcvd_frames;

    //! Rough reference plane of the projection
    Plane _plane_ref;

    //! Minimum depth of the current observed scene
    float _depth_min_current;

    //! Maximum depth of the current observed scene
    float _depth_max_current;

    //! Buffer for frames that should be 3d reconstructed
    std::deque<Frame::Ptr> _buffer_reco;
    std::mutex _mutex_buffer_reco;

    //! Buffer for consistency filter
    std::deque<std::pair<Frame::Ptr, cv::Mat>> _buffer_consistency;

    //! Densifier handle for surface reconstruction. Mostly external frameworks to generate dense depth maps
    DensifierIF::Ptr _densifier;

    /*!
     * @brief Function to call for reset of densification stage
     */
    void reset() override;

    /*!
     * @brief Callback function after the stage received the current output folder
     */
    void initStageCallback() override;

    /*!
     * @brief Function that prints all stage settings into a log file
     */
    void printSettingsToLog() override;

    /*!
     * @brief Publish function that is called on every iteration of the processing thread
     * @param frame Current frame to be processed
     * @param depthmap Depthmap for the current frame
     */
    void publish(const Frame::Ptr &frame, const cv::Mat &depthmap);

    /*!
     * @brief Save funtion that is called on every iteration of the processing thread
     * @param frame Current frame to be processed
     * @param normals Current normal map
     * @param mask Currnet mask of all valid values of the depthmap/normal map
     */
    void saveIter(const Frame::Ptr &frame, const cv::Mat &normals);

    /*!
     * @brief Function to push the input frame to the reconstruction buffer. Frames from different UAVs will be treated
     *        separately, identified by the "CameraId" of the frame
     * @param frame Current input frame
     */
    void pushToBufferReco(const Frame::Ptr &frame);

    /*!
     * @brief Function to remove the oldest frame from the "reconstruction" buffer.
     */
    void popFromBufferReco();

    /*!
     * @brief Function to apply depth map filtering to current input depth map
     * @param depthmap Depth map to be filtered
     * @return Filtered depth map
     */
    cv::Mat applyDepthMapPostProcessing(const cv::Mat &depthmap);

    /*!
     * @brief Sets all depth values outside the given range to -1.0, so invalid.
     * @param depthmap Depthmap which should be processed
     * @param min_depth Minimum depth value that is allowed
     * @param max_depth Maximum depth value that is allowed
     * @return Resulting depthmap
     */
    Depthmap::Ptr forceInRange(const Depthmap::Ptr &depthmap, double min_depth, double max_depth);

    /*!
     * @brief
     * @param buffer_denoise
     * @return
     */
    Frame::Ptr consistencyFilter(std::deque<std::pair<Frame::Ptr, cv::Mat>>* buffer_denoise);

    /*!
     * @brief Function to compute a mask for the current depth map for valid elements.
     * @param depth_map Reconstructed depth map
     * @param use_sparse_mask Set true to compute polygon around all sparse points, so regions with no sparse points
     *        are masked out.
     * @return Mask for the input depth map
     */
    cv::Mat computeDepthMapMask(const cv::Mat &depth_map, bool use_sparse_mask);

    /*!
     * @brief Process function to compute 3d surface reconstruction from input frame.
     * @param buffer The buffer of frames for which the depth map should be reconstructed.
     * @param depthmap Output dense depth map
     * @return True if successful
     */
    Depthmap::Ptr processStereoReconstruction(const std::deque<Frame::Ptr> &buffer, Frame::Ptr &frame_processed);
};

} // namespace stages
} // namespace realm

#endif //PROJECT_DENSIFICATION_STAGE_H
