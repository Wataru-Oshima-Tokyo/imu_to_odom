#include "imu_to_odom/imu_to_odom.h"

OdomPredictor::OdomPredictor(const ros::NodeHandle& nh,
                             const ros::NodeHandle& nh_private)
    : nh_(nh),
      nh_private_(nh_private),
      seq_(0),
      have_odom_(false),
      have_bias_(false) {
  nh_private.param("max_imu_queue_length", max_imu_queue_length_, 1000);

  constexpr size_t kROSQueueLength = 100;
  imu_sub_ =
      nh_.subscribe("imu", kROSQueueLength, &OdomPredictor::imuCallback, this);
  imu_bias_sub_ = nh_.subscribe("imu_bias", kROSQueueLength,
                                &OdomPredictor::imuBiasCallback, this);
  //odometry_sub_ = nh_.subscribe("odometry", kROSQueueLength,
  //                              &OdomPredictor::odometryCallback, this);

  odom_pub_ = nh_private_.advertise<nav_msgs::Odometry>("imu_odometry",
                                                        kROSQueueLength);
  transform_pub_ = nh_private_.advertise<geometry_msgs::TransformStamped>(
      "imu_transform", kROSQueueLength);


  // Initialize pose
  //estimate_timestamp_ = ros::Time::now(); // -1
  //bool has_imu_meas = false; 

  geometry_msgs::Point pos;
  geometry_msgs::Pose pose;

  pos.x = 0; 
  pos.y = 0; 
  pos.z = 0;
  
  geometry_msgs::Quaternion quat;
  quat.x = 0;
  quat.y = 0;
  quat.z = 0;
  quat.w = 1.0;
  pose.orientation = quat;
  
  pose.position = pos;

  tf::poseMsgToKindr(pose, &transform_);

  // itialize the rest
  /*
  c =                 {1 0 0 0 0 0
                      0 1 0 0 0 0 
                      0 0 1 0 0 0
                      0 0 0 1 0 0 
                      0 0 0 0 1 0 
                      0 0 0 0 0 1};
  */
  linear_velocity_ = {0, 0, 0};
  angular_velocity_ = {0, 0, 0};
  /*
  twist_covariance_ = {1 0 0 0 0 0
                      0 1 0 0 0 0 
                      0 0 1 0 0 0
                      0 0 0 1 0 0 
                      0 0 0 0 1 0 
                      0 0 0 0 0 1};
  */
  frame_id_ = "world";
  child_frame_id_ = "drone";
}
/*
void OdomPredictor::odometryCallback(const nav_msgs::OdometryConstPtr& msg) {
  if (!have_bias_) {
    return;
  }

  // clear old IMU measurements
  while (!imu_queue_.empty() &&
         imu_queue_.front().header.stamp < msg->header.stamp) {
    imu_queue_.pop_front();
  }

  // extract useful information from message
  tf::poseMsgToKindr(msg->pose.pose, &transform_);
  pose_covariance_ = msg->pose.covariance;
  tf::vectorMsgToKindr(msg->twist.twist.linear, &linear_velocity_);
  tf::vectorMsgToKindr(msg->twist.twist.angular, &angular_velocity_);
  twist_covariance_ = msg->twist.covariance;
  frame_id_ = msg->header.frame_id;
  child_frame_id_ = msg->child_frame_id;

  // reintegrate IMU messages
  estimate_timestamp_ = msg->header.stamp;

  try {
    for (const sensor_msgs::Imu& imu_msg : imu_queue_) {
      integrateIMUData(imu_msg);
    }
  } catch (std::exception& e) {
    ROS_ERROR_STREAM(
        "IMU INTEGRATION FAILED, RESETING EVERYTHING: " << e.what());
    have_bias_ = false;
    have_odom_ = false;
    imu_queue_.clear();
    return;
  }

  have_odom_ = true;
}
*/

void OdomPredictor::imuCallback(const sensor_msgs::ImuConstPtr& msg) {
  
  //tf::poseMsgToKindr(pose, &transform_);

  // if we have orientation
  
  //normalize
  /*
  tf2::Quaternion quat_tf;
  tf2::fromMsg(msg->orientation, quat_tf);
  quat_tf.normalize();
  geometry_msgs::Quaternion quat = tf2::toMsg(quat_tf);
  */
  //normalize

  ROS_ERROR_STREAM("IMU INTEGRATION FAILED, RESETING EVERYTHING: %f, %f, %f, %f" << msg->orientation.x << msg->orientation.y << msg->orientation.z << msg->orientation.w);
  tf::quaternionMsgToKindr(msg->orientation, &orientation_);
  transform_.getRotation() = orientation_;
  
  if (msg->header.stamp < imu_queue_.back().header.stamp) {
    ROS_ERROR_STREAM("Latest IMU message occured at time: "
                     << msg->header.stamp
                     << ". This is before the previously received IMU "
                        "message that ouccured at: "
                     << imu_queue_.back().header.stamp
                     << ". The current imu queue will be reset.");
    imu_queue_.clear();
  }
  /*
  if (imu_queue_.size() > max_imu_queue_length_) {
    ROS_WARN_STREAM_THROTTLE(
        10, "There has been over "
                << max_imu_queue_length_
                << " IMU messages since the last odometry update. The oldest "
                   "measurement will be forgotten. This message is printed "
                   "once every 10 seconds");
    imu_queue_.pop_front();
  }
  */

  imu_queue_.push_back(*msg);

  /*
  if (!have_bias_ || !have_odom_) {
    return;
  }
  */

  try {
    integrateIMUData(*msg);
  } catch (std::exception& e) {
    ROS_ERROR_STREAM(
        "IMU INTEGRATION FAILED, RESETING EVERYTHING: " << e.what());
    have_bias_ = false;
    have_odom_ = false;
    imu_queue_.clear();
    return;
  }

  publishOdometry();
  publishTF();
  ++seq_;
}

void OdomPredictor::imuBiasCallback(const sensor_msgs::ImuConstPtr& msg) {
  tf::vectorMsgToKindr(msg->linear_acceleration,
                       &imu_linear_acceleration_bias_);
  tf::vectorMsgToKindr(msg->angular_velocity, &imu_angular_velocity_bias_);

  have_bias_ = true;
}

void OdomPredictor::integrateIMUData(const sensor_msgs::Imu& msg) {
  if (!has_imu_meas) {
    estimate_timestamp_ = msg.header.stamp;
    has_imu_meas = true;
    return;
  }

  ROS_ERROR_STREAM("Doing imu integration");

  const double delta_time = (msg.header.stamp - estimate_timestamp_).toSec();

  const Vector3 kGravity(0.0, 0.0, -9.81);

  Vector3 imu_linear_acceleration, imu_angular_velocity;
  tf::vectorMsgToKindr(msg.linear_acceleration, &imu_linear_acceleration);
  tf::vectorMsgToKindr(msg.angular_velocity, &imu_angular_velocity);

  // find changes in angular velocity and rotation delta
  /*
  const Vector3 final_angular_velocity =
      (imu_angular_velocity - imu_angular_velocity_bias_);
  const Vector3 delta_angle =
      delta_time * (final_angular_velocity + angular_velocity_) / 2.0;
  angular_velocity_ = final_angular_velocity;
  */

  // apply half of the rotation delta
  /*
  const Rotation half_delta_rotation = Rotation::exp(delta_angle / 2.0);
  transform_.getRotation() = transform_.getRotation() * half_delta_rotation;
  */

  // find changes in linear velocity and position
  const Vector3 delta_linear_velocity =
      delta_time * (imu_linear_acceleration +
                    transform_.getRotation().inverse().rotate(kGravity) );//  -
                    //imu_linear_acceleration_bias_);
  transform_.getPosition() =
      transform_.getPosition() +
      transform_.getRotation().rotate(
          delta_time * (linear_velocity_ + delta_linear_velocity / 2.0));
  linear_velocity_ += delta_linear_velocity; // AP2020 : change in displacement should be rotated with curr rotation

  // apply the other half of the rotation delta
  //transform_.getRotation() = transform_.getRotation() * half_delta_rotation;

  estimate_timestamp_ = msg.header.stamp;
}

void OdomPredictor::publishOdometry() {
  if (!have_odom_) {
    return;
  }

  nav_msgs::Odometry msg;

  msg.header.frame_id = frame_id_;
  msg.header.seq = seq_;
  msg.header.stamp = estimate_timestamp_;
  msg.child_frame_id = child_frame_id_;

  tf::poseKindrToMsg(transform_, &msg.pose.pose);
  //msg.pose.covariance = pose_covariance_;

  tf::vectorKindrToMsg(linear_velocity_, &msg.twist.twist.linear);
  tf::vectorKindrToMsg(angular_velocity_, &msg.twist.twist.angular);
  //msg.twist.covariance = twist_covariance_;

  odom_pub_.publish(msg);
}

void OdomPredictor::publishTF() {
  if (!have_odom_) {
    return;
  }

  geometry_msgs::TransformStamped msg;

  msg.header.frame_id = frame_id_;
  msg.header.seq = seq_;
  msg.header.stamp = estimate_timestamp_;
  msg.child_frame_id = child_frame_id_;

  tf::transformKindrToMsg(transform_, &msg.transform);

  transform_pub_.publish(msg);
  br_.sendTransform(msg);
}
