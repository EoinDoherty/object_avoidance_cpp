/* @file FOF.cpp
 * @brief Flow of Flow processing and control command calulation
 */

#include <ros/ros.h>
#include <ros/console.h>
#include <math.h>
#include <std_msgs/Int16.h>
#include <std_msgs/Bool.h>
#include <std_msgs/String.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <mavros_msgs/RCIn.h>
#include <std_msgs/Float32.h>
#include <object_avoidance_cpp/YawRateCmdMsg.h>
#include <object_avoidance_cpp/FlowRingOutMsg.h>
#include <iostream>
using namespace std;

class SubscribeAndPublish{
private:
    ros::NodeHandle nh_;
    ros::Publisher pub_;
    ros::Subscriber sub_;
    
    std::vector<float> Qdot_u;
    std::vector<float> Qdot_v;
    
    float dt, tau, k_0, alpha, c_psi, c_d;
    float mean_val, mean_sum, std_dev, r_0, d_0;
    float average_OF_tang[];
    float OF_tang_prev[60];
    float OF_tang[60]; 
    float R_FOF[60];
    float LP_OF[60]; 
    int num_rings; 
    int index; 
    float yaw_rate_cmd;
    float min_threshold;
    object_avoidance_cpp::YawRateCmdMsg yaw_rate_cmd_msg; 
    int sign;
    float gamma_arr[60];
    object_avoidance_cpp::FlowRingOutMsg current_flow;

public:
    SubscribeAndPublish(){
        // Create the yaw rate command publisher
        ROS_INFO("Object Created");
        pub_ = nh_.advertise<object_avoidance_cpp::YawRateCmdMsg>
             ("/yaw_cmd_out", 10);

        //Initialize the gamma array
        for(int i = 0; i < 60; i++){
           gamma_arr[i] = (2*M_PI/60)*i;
           R_FOF[i] = 0.0;
           LP_OF[i] = 0.0;
           OF_tang[i] = 0.0;
           average_OF_tang[i] = 0.0;
        }
         
    }
    void flow_cb(const object_avoidance_cpp::FlowRingOutMsg::ConstPtr& msg);
}; // End of class SubscribeAndPublish

void SubscribeAndPublish::flow_cb(const object_avoidance_cpp::FlowRingOutMsg::ConstPtr& msg)
{

        current_flow = *msg;
        Qdot_u = current_flow.Qdot_u;
        Qdot_v = current_flow.Qdot_v;
        for(int i = 0; i < 60; i++){
            OF_tang_prev[i] = OF_tang[i];
        }
        //ROS_INF0("%f",Qdot_u[1]);
        // Initialize all variables
        dt = .5;
        tau = .75;
        alpha = dt/(tau+dt);
        k_0 = .3;
        c_psi = -1;
        c_d = .25;
        mean_val = 0.0;
        mean_sum = 0.0;
        r_0 = 0.0;
        d_0 = 0.0;
        num_rings = 5;
        index = 0;
        yaw_rate_cmd = 0.0;
        min_threshold = 0.0;


        // Compute average ring flow
        for(int r = 0; r< 5; r++){
            for(int i = 0; i < 60; i++){
                index = r*60 + i;
                average_OF_tang[i] = average_OF_tang[i] + (Qdot_u[index]*sin(gamma_arr[i])-1*Qdot_v[index]*cos(gamma_arr[i]));
            }
        }

        // Compute the spatial average
        for(int i = 0; i < 60; i++) average_OF_tang[i] *= .2; 

        // Reformat the vector to -pi to pi
        for(int i = 0; i < 60; i++){
            if(i < 30){
                OF_tang[i] = -average_OF_tang[30 - i];
            }
            if(i >= 30){
                OF_tang[i] = average_OF_tang[(90 - 1) - i];
            }
        }

        // Compute the FOF control
        // // // // // // // // //
    
        // Run the LPF
        for(int i = 0; i < 60; i++){
            LP_OF[i] = alpha*OF_tang[i] + (1 - alpha)*OF_tang_prev[i];   
        }

        // Final FOF Calc
        for(int i = 0; i < 59; i++){
            R_FOF[i] = LP_OF[i]*OF_tang[i+1] - OF_tang[i]*LP_OF[i+1];
        }

        R_FOF[59] = 0.0;
        mean_sum = 0.0;
        std_dev = 0.0;
        // Dynamic Threshold
        for(int i = 0; i < 60; i++){
            mean_sum += R_FOF[i];
        }
        mean_val = mean_sum / 60;

        for(int i = 0; i < 60; i++){
            std_dev += pow((R_FOF[i] - mean_val), 2);
        }
        std_dev = pow(std_dev / 60, 0.5);
        min_threshold = 3*std_dev;

        // Extract r_0 and d_0 from process OF signal
        for(int i = 0; i < 60; i++){
            if(R_FOF[i] > r_0){
                d_0 = R_FOF[i];
                r_0 = gamma_arr[i];
            }
        }

        if(r_0 > 0){sign = 1;}
        else{sign = -1;}
  
        // Calculate the control signal
        if(d_0 > min_threshold){
            yaw_rate_cmd_msg.yaw_rate_cmd = k_0*sign*exp(-c_psi*abs(r_0))*exp(-c_d/abs(d_0));
        } else {
            yaw_rate_cmd_msg.yaw_rate_cmd = 0.0;
        }
        pub_.publish(yaw_rate_cmd_msg);    
    // End of flow calc  
}

int main(int argc, char **argv){
    ros::init(argc, argv, "FOF_node");
    ros::NodeHandle n;
    
    SubscribeAndPublish FlowSubPubObject;
    
    ros::Subscriber sub = n.subscribe("/flow_rings",1000, &SubscribeAndPublish::flow_cb, &FlowSubPubObject);
    
    ros::spin();
    
    return 0; // Exit main loop     
}
