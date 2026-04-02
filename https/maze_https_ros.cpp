#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>

/* ROS2 */
#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"

#define DEFAULT_PORT 8449

/* ---------------- ROS2 GLOBALS ---------------- */
rclcpp::Node::SharedPtr node;
rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr pub;

pthread_t ros_thread;
pthread_t control_thread;

volatile double vel_x = 0.0;
volatile double vel_z = 0.0;

/* ---------------- ROS2 THREAD ---------------- */
void* ros_spin_thread(void*)
{
    rclcpp::spin(node);
    return NULL;
}

/* ---------------- MOTION LOOP (REAL-TIME STREAM) ---------------- */
void* cmdvel_loop(void*)
{
    rclcpp::Rate rate(50); // 50 Hz (IMPORTANT)

    while (rclcpp::ok())
    {
        geometry_msgs::msg::Twist msg;
        msg.linear.x = vel_x;
        msg.angular.z = vel_z;

        pub->publish(msg);

        rate.sleep();
    }

    return NULL;
}

/* ---------------- MOVEMENT API ---------------- */
void move_forward()
{
    vel_x = 0.15;
    vel_z = 0.0;
}

void move_backward()
{
    vel_x = -0.15;
    vel_z = 0.0;
}

void move_left()
{
    vel_x = 0.0;
    vel_z = 0.5;
}

void move_right()
{
    vel_x = 0.0;
    vel_z = -0.5;
}

void stop_move()
{
    vel_x = 0.0;
    vel_z = 0.0;
}

/* ---------------- HTTP SERVER ---------------- */

struct connection_info {
    char *data;
    size_t size;
};

static int handle_post(void *cls,
                       struct MHD_Connection *connection,
                       const char *url,
                       const char *method,
                       const char *version,
                       const char *upload_data,
                       size_t *upload_data_size,
                       void **con_cls)
{
    (void)cls; (void)version;

    if (strcmp(method, "POST") != 0 || strcmp(url, "/move") != 0)
        return MHD_NO;

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(*ci));
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci = (struct connection_info*)*con_cls;

    if (*upload_data_size != 0) {
        ci->data = (char*)realloc(ci->data, ci->size + *upload_data_size + 1);
        memcpy(ci->data + ci->size, upload_data, *upload_data_size);
        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    /* -------- parse move_dir -------- */
    char *move_dir = NULL;
    char *found = strstr(ci->data, "\"move_dir\"");

    if (found)
    {
        char *colon = strchr(found, ':');
        if (colon)
        {
            colon++;
            while (isspace(*colon) || *colon=='\"') colon++;

            char *end = colon;
            while (*end && *end != '\"' && *end != ',' && *end != '}') end++;
            *end = '\0';

            move_dir = colon;
        }
    }

    /* -------- instant command update -------- */
    if (move_dir)
    {
        if (strcmp(move_dir, "forward") == 0) move_forward();
        else if (strcmp(move_dir, "backward") == 0) move_backward();
        else if (strcmp(move_dir, "left") == 0) move_left();
        else if (strcmp(move_dir, "right") == 0) move_right();
        else stop_move();
    }

    const char *response = "{\"status\":\"ok\"}";
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(response),
                                        (void*)response,
                                        MHD_RESPMEM_PERSISTENT);

    int ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return ret;
}

/* ---------------- MAIN ---------------- */
int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);

    node = std::make_shared<rclcpp::Node>("cmd_vel_http_bridge");

    pub = node->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);

    /* start ROS spinning thread */
    pthread_create(&ros_thread, NULL, ros_spin_thread, NULL);

    /* start 50Hz command streaming thread */
    pthread_create(&control_thread, NULL, cmdvel_loop, NULL);

    struct MHD_Daemon *daemon =
        MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD,
                         DEFAULT_PORT,
                         NULL, NULL,
                         &handle_post, NULL,
                         MHD_OPTION_END);

    if (!daemon)
    {
        printf("Failed to start HTTP server\n");
        return 1;
    }

    printf("Server running on port %d\n", DEFAULT_PORT);
    printf("POST /move {\"move_dir\":\"forward\"}\n");

    getchar();

    MHD_stop_daemon(daemon);
    rclcpp::shutdown();

    return 0;
}