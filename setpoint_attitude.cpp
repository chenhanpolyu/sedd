/**
 * @file offb_node.cpp
 * @brief Offboard control example node, written with MAVROS version 0.19.x, PX4 Pro Flight
 * Stack and tested in Gazebo SITL
 */

#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <mavros_msgs/Thrust.h>
#include <geometry_msgs/TwistStamped.h>
#include <mavros_msgs/AttitudeTarget.h>
#include <sensor_msgs/Imu.h>
#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Geometry>

mavros_msgs::State current_state;
void state_cb(const mavros_msgs::State::ConstPtr& msg){
    current_state = *msg;
}

struct attitude_quat{
    float q1;
    float q2;
    float q3;
    float q4;
};

float _roll,_pitch,_yaw;
struct attitude_quat att_quat;
sensor_msgs::Imu current_imudata;
Eigen::Quaternionf current_imu_quat;

float get_euler_roll(float q1,float q2,float q3,float q4)
{
    return (atan2f(2.0f*(q1*q2 + q3*q4), 1.0f - 2.0f*(q2*q2 + q3*q3)));
}

// get euler pitch angle
float get_euler_pitch(float q1,float q2,float q3,float q4) 
{
    return asin(2.0f*(q1*q3 - q4*q2));
}

// get euler yaw angle
float get_euler_yaw(float q1,float q2,float q3,float q4)
{
    return atan2f(2.0f*(q1*q4 + q2*q3), 1.0f - 2.0f*(q3*q3 + q4*q4));
}

// create eulers from a quaternion
void to_euler(float &roll, float &pitch, float &yaw)
{
    roll = get_euler_roll(current_imudata.orientation.w,current_imudata.orientation.x,current_imudata.orientation.y,current_imudata.orientation.z);
    pitch = get_euler_pitch(current_imudata.orientation.w,current_imudata.orientation.x,current_imudata.orientation.y,current_imudata.orientation.z);
    yaw = get_euler_yaw(current_imudata.orientation.w,current_imudata.orientation.x,current_imudata.orientation.y,current_imudata.orientation.z);
}

void imu_cb(const sensor_msgs::Imu::ConstPtr& msg){
    current_imudata = *msg;
    current_imu_quat.w() = current_imudata.orientation.w;
    current_imu_quat.x() = current_imudata.orientation.x;
    current_imu_quat.y() = current_imudata.orientation.y;
    current_imu_quat.z() = current_imudata.orientation.z;
    to_euler(_roll,_pitch,_yaw);
   // ROS_INFO("PASS THE IMU DATA %f",current_imudata.orientation.w);
}

void from_euler(float roll, float pitch, float yaw)
{
    const float cr2 = cosf(roll*0.5f);
    const float cp2 = cosf(pitch*0.5f);
    const float cy2 = cosf(yaw*0.5f);
    const float sr2 = sinf(roll*0.5f);
    const float sp2 = sinf(pitch*0.5f);
    const float sy2 = sinf(yaw*0.5f);

    att_quat.q1 = cr2*cp2*cy2 + sr2*sp2*sy2;
    att_quat.q2 = sr2*cp2*cy2 - cr2*sp2*sy2;
    att_quat.q3 = cr2*sp2*cy2 + sr2*cp2*sy2;
    att_quat.q4 = cr2*cp2*sy2 - sr2*sp2*cy2;
}



int main(int argc, char **argv)
{
    ros::init(argc, argv, "offb_node");
    ros::NodeHandle nh;

    ros::Subscriber state_sub = nh.subscribe<mavros_msgs::State>
            ("mavros/state", 10, state_cb);
    ros::Publisher local_attitude_pub = nh.advertise<mavros_msgs::AttitudeTarget>("mavros/setpoint_raw/attitude",10);
    ros::Subscriber imu_attitude_sub = nh.subscribe<sensor_msgs::Imu>("mavros/imu/data",10,imu_cb);
    ros::Publisher local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>
            ("mavros/setpoint_position/local", 10);
    ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>
            ("mavros/cmd/arming");
    ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>
            ("mavros/set_mode");

    //the setpoint publishing rate MUST be faster than 2Hz
    ros::Rate rate(20.0);

    // wait for FCU connection
    while(ros::ok() && !current_state.connected){
        ros::spinOnce();
        rate.sleep();
    }

    geometry_msgs::PoseStamped pose;
    pose.pose.position.x = 1;
    pose.pose.position.y = 2;
    pose.pose.position.z = 5;

    float roll_deg = 0;
    float pitch_deg = 0;
    float yaw_deg = 0;

    from_euler(roll_deg*M_PI/180, pitch_deg*M_PI/180, yaw_deg*M_PI/180);

    mavros_msgs::AttitudeTarget attitude_raw;
    attitude_raw.orientation.w = att_quat.q1;
    attitude_raw.orientation.x = att_quat.q2;
    attitude_raw.orientation.y = att_quat.q3;
    attitude_raw.orientation.z = att_quat.q4;
    attitude_raw.thrust = 0.7059122;
    attitude_raw.type_mask = 0b00000111;

    pose.pose.orientation.w = att_quat.q1;
    pose.pose.orientation.x = att_quat.q2;
    pose.pose.orientation.y = att_quat.q3;
    pose.pose.orientation.z = att_quat.q4;

    mavros_msgs::SetMode offb_set_mode;
    offb_set_mode.request.custom_mode = "OFFBOARD";

    mavros_msgs::CommandBool arm_cmd;
    arm_cmd.request.value = true;

    ros::Time last_request = ros::Time::now();
    int16_t step_counter = 0;
    int8_t step_one = 1;
    Eigen::Matrix3f _rotMatrix;
    Eigen::Vector3f eular_angle;
    while(ros::ok()){
        _rotMatrix = current_imu_quat.toRotationMatrix();
        eular_angle = _rotMatrix.eulerAngles(2,1,0);
        if(!(step_counter%20))
            ROS_INFO("Imu_data = %f %f %f", _roll*180/M_PI,_pitch*180/M_PI,_yaw*180/M_PI);
        if( current_state.mode != "OFFBOARD" &&
            (ros::Time::now() - last_request > ros::Duration(5.0))){
            if( set_mode_client.call(offb_set_mode) &&
                offb_set_mode.response.mode_sent){
                ROS_INFO("Offboard enabled");
            }
            last_request = ros::Time::now();
        } else {

            if( !current_state.armed &&
                (ros::Time::now() - last_request > ros::Duration(5.0)))
                {
                if( arming_client.call(arm_cmd) &&
                    arm_cmd.response.success){
                    ROS_INFO("Vehicle armed");
                }
                last_request = ros::Time::now();
                ROS_INFO("try to arm");
            }
            else{
                if(step_counter<400){
                    step_one = 1;
                }     
                else
                {
                    step_one = 0;
                    ROS_INFO("Start to use attitude!");
                }
                
            }
        }

        if(step_one){
            local_pos_pub.publish(pose);
        }   
        else
        {
            local_attitude_pub.publish(attitude_raw);
        }
        ros::spinOnce();
        rate.sleep();
        step_counter++;
    }

    return 0;
}
