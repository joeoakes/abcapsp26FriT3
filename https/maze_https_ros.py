import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
from flask import Flask, request, jsonify
import threading
import time
import ssl

# ---------------- ROS NODE ---------------- #
class PupperController(Node):
    def __init__(self):
        super().__init__('pupper_controller')
        self.publisher = self.create_publisher(Twist, '/cmd_vel', 10)

    def publish_cmd(self, linear_x=0.0, angular_z=0.0, duration=0.0):
        msg = Twist()
        msg.linear.x = linear_x
        msg.angular.z = angular_z

        start_time = time.time()
        while time.time() - start_time < duration:
            self.publisher.publish(msg)
            time.sleep(0.1)

        # Stop after movement
        stop_msg = Twist()
        self.publisher.publish(stop_msg)

    # Movement functions
    def move_forward(self):
        self.publish_cmd(linear_x=0.15, duration=2)

    def move_backward(self):
        self.publish_cmd(linear_x=-0.15, duration=2)

    def move_left(self):
        self.publish_cmd(angular_z=0.5, duration=3)
        self.move_forward()

    def move_right(self):
        self.publish_cmd(angular_z=-0.5, duration=3)
        self.move_forward()


# ---------------- FLASK SERVER ---------------- #
app = Flask(__name__)
controller = None

@app.route('/move', methods=['POST'])
def move():
    data = request.get_json()

    if not data or "input" not in data or "move_dir" not in data["input"]:
        return jsonify({"error": "move_dir not provided"}), 400

    move_dir = data["input"]["move_dir"]

    if move_dir == "forward":
        controller.move_forward()
    elif move_dir == "backward":
        controller.move_backward()
    elif move_dir == "left":
        controller.move_left()
    elif move_dir == "right":
        controller.move_right()
    else:
        return jsonify({"error": f"Unknown move_dir: {move_dir}"}), 400

    return jsonify({"status": "ok"})


# ---------------- MAIN ---------------- #
def main():
    global controller

    rclpy.init()
    controller = PupperController()

    # Run ROS in background thread
    ros_thread = threading.Thread(target=rclpy.spin, args=(controller,), daemon=True)
    ros_thread.start()

    # HTTPS setup
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain('certs/server.crt', 'certs/server.key')

    print("--------------------------------------------------")
    print("HTTPS server running at https://0.0.0.0:8449")
    print("POST JSON to /move")
    print("--------------------------------------------------")

    app.run(host='0.0.0.0', port=8449, ssl_context=context)

    controller.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()