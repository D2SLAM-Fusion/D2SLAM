#include "visualization.hpp"
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/PointCloud.h>
#include "../estimator/d2estimator.hpp"
#include "CameraPoseVisualization.h"

namespace D2VINS {
sensor_msgs::PointCloud toPointCloud(const std::vector<D2Common::LandmarkPerId> landmarks, bool use_raw_color = false);

std::vector<Eigen::Vector3d> D2Visualization::drone_colors{
        Vector3d(1, 1, 0), //drone 0 yellow
        Vector3d(1, 0, 0), //drone 1 red
        Vector3d(0, 1, 0), //drone 2 green
        Vector3d(0, 0, 1), //drone 3 blue
        Vector3d(0, 1, 1), //drone 4 cyan
        Vector3d(1, 0, 1), //drone 5 magenta
        Vector3d(1, 1, 1), //drone 6 white
        Vector3d(0, 0, 0), //drone 7 black
        Vector3d(0.5, 0.5, 0.5), //drone 8 gray
        Vector3d(0.5, 0, 0), //drone 9 orange
        Vector3d(0, 0.5, 0), //drone 10 green
        Vector3d(0, 0, 0.5), //drone 11 blue
        Vector3d(0.5, 0, 0.5), //drone 12 purple
        Vector3d(0.5, 0.5, 0), //drone 13 orange
        Vector3d(0, 0.5, 0.5), //drone 14 cyan
        Vector3d(0.5, 0.5, 0.5) //drone 15 white
};

D2Visualization::D2Visualization()
{}

void D2Visualization::init(ros::NodeHandle & nh, D2Estimator * estimator) {
    pcl_pub = nh.advertise<sensor_msgs::PointCloud>("point_cloud", 1000);
    margined_pcl = nh.advertise<sensor_msgs::PointCloud>("margined_cloud", 1000);
    odom_pub = nh.advertise<nav_msgs::Odometry>("odometry", 1000);
    imu_prop_pub = nh.advertise<nav_msgs::Odometry>("imu_propagation", 1000);
    path_pub = nh.advertise<nav_msgs::Path>("path", 1000);
    sld_win_pub = nh.advertise<visualization_msgs::MarkerArray>("slding_window", 1000);
    frame_pub_local = nh.advertise<swarm_msgs::VIOFrame>("frame_local", 1000);
    frame_pub_remote = nh.advertise<swarm_msgs::VIOFrame>("frame_remote", 1000);
    for (int i = 0; i < estimator->getState().localCameraExtrinsics().size(); i++) {
        char topic_name[64] = {0};
        sprintf(topic_name, "camera_pose_%d", i);
        camera_pose_pubs.emplace_back(nh.advertise<geometry_msgs::PoseStamped>(topic_name, 1000));
    }
    _estimator = estimator;
    _nh = &nh;
}

void D2Visualization::pubFrame(D2Common::VINSFrame* frame) {
    if (frame == nullptr) {
        return;
    }
    //Publish the VINSFrame
    if (frame->drone_id == params->self_id) {
        auto exts = _estimator->getState().localCameraExtrinsics();
        swarm_msgs::VIOFrame msg = frame->toROS(exts);
        frame_pub_local.publish(msg);
    } else {
        swarm_msgs::VIOFrame msg = frame->toROS();
        frame_pub_remote.publish(msg);
    }
}

void D2Visualization::postSolve() {
    auto & state = _estimator->getState();
    if (params->estimation_mode < D2VINSConfig::SERVER_MODE) {
        auto imu_prop = _estimator->getImuPropagation().toRos();
        imu_prop_pub.publish(imu_prop);
    }
    auto pcl = state.getInitializedLandmarks();
    pcl_pub.publish(toPointCloud(pcl));
    margined_pcl.publish(toPointCloud(_estimator->getMarginedLandmarks(), true));

    for (auto drone_id: state.availableDrones()) {
        auto odom = _estimator->getOdometry(drone_id);
        auto odom_ros = odom.toRos();
        if (paths.find(drone_id) != paths.end() && (odom_ros.header.stamp - paths[drone_id].header.stamp).toSec() < 1e-3) {
            continue;
        }
        auto & path = paths[drone_id];
        if (odom_pubs.find(drone_id) == odom_pubs.end()) {
            odom_pubs[drone_id] = _nh->advertise<nav_msgs::Odometry>("odometry_" + std::to_string(drone_id), 1000);
            path_pubs[drone_id] = _nh->advertise<nav_msgs::Path>("path_" + std::to_string(drone_id), 1000);
            csv_output_files[drone_id] = std::ofstream(params->output_folder + "/d2vins_" + std::to_string(drone_id) + ".csv", std::ios::out);
        }
        geometry_msgs::PoseStamped pose_stamped;
        pose_stamped.header = odom_ros.header;
        pose_stamped.header.frame_id = "world";
        pose_stamped.pose = odom_ros.pose.pose;
        path.header = odom_ros.header;
        path.header.frame_id = "world";
        path.poses.push_back(pose_stamped);
        
        if (drone_id == params->self_id) {
            path_pub.publish(path);
            odom_pub.publish(odom_ros);
            auto exts = state.localCameraExtrinsics();
            for (int i = 0; i < exts.size(); i ++) {
                auto camera_pose = exts[i];
                auto pose = (odom.pose()*camera_pose).toROS();
                geometry_msgs::PoseStamped camera_pose_ros;
                camera_pose_ros.header = odom_ros.header;
                camera_pose_ros.header.frame_id = "world";
                camera_pose_ros.pose = pose;
                camera_pose_pubs[i].publish(camera_pose_ros);
            }
        }
        csv_output_files[drone_id] << std::setprecision(std::numeric_limits<long double>::digits10 + 1) << odom.stamp << " " << odom.pose().toStr(true) << std::endl;
        odom_pubs[drone_id].publish(odom_ros);
        path_pubs[drone_id].publish(path);
    }

    CameraPoseVisualization cam_visual;
    for (auto drone_id: state.availableDrones()) {
        for (int i = 0; i < state.size(drone_id); i ++) {
            auto & frame = state.getFrame(drone_id, i);
            CamIdType camera_id = *state.getAvailableCameraIds().begin();
            auto frame_pose = frame.odom.pose();
            if (params->estimation_mode == D2VINSConfig::DISTRIBUTED_CAMERA_CONSENUS) {
                
            }
            auto cam_pose = frame.odom.pose()*state.getExtrinsic(camera_id);
            cam_visual.addPose(cam_pose.pos(), cam_pose.att(), drone_colors[drone_id], display_alpha);
        }
    }
    std_msgs::Header header;
    header.frame_id = "world";
    header.stamp = ros::Time::now();
    cam_visual.publishBy(sld_win_pub, header);
}

sensor_msgs::PointCloud toPointCloud(const std::vector<D2Common::LandmarkPerId> landmarks, bool use_raw_color) {
    sensor_msgs::PointCloud pcl;
    pcl.header.frame_id = "world";
    pcl.points.resize(landmarks.size());
    pcl.channels.resize(3);
    pcl.channels[0].name = "rgb";
    pcl.channels[0].values.resize(landmarks.size());
    for (int i = 0; i < landmarks.size(); i++) {
        pcl.points[i].x = landmarks[i].position.x();
        pcl.points[i].y = landmarks[i].position.y();
        pcl.points[i].z = landmarks[i].position.z();
        Vector3i color(255, 255, 0.);
        if (use_raw_color) {
            color = Vector3i(landmarks[i].color[2], landmarks[i].color[1], landmarks[i].color[0]);
        } else {
            // if (landmarks[i].flag == D2Common::LandmarkFlag::ESTIMATED) {
            //     //set color to green
            //     color = Vector3i(0, 255, 0.);
            // } else if (landmarks[i].flag == D2Common::LandmarkFlag::OUTLIER) {
            //     //set color to gray
            //     color = Vector3i(200, 200, 200.);
            // }
            //Set color with drone id
            color = (D2Visualization::drone_colors[landmarks[i].track[0].drone_id]*255).template cast<int>();
        }
        uint32_t hex_r = (0xff & color(0)) << 16;
        uint32_t hex_g = (0xff & color(1)) << 8;
        uint32_t hex_b = (0xff & color(2));
        uint32_t hex_rgb = hex_r | hex_g | hex_b;
        memcpy(pcl.channels[0].values.data() + i, &hex_rgb, sizeof(float));
    }
    return pcl;
}
}