import cv2
import numpy as np
from pupil_apriltags import Detector
import math

def main():
    # ---- Camera selection ----
    cam_index = 0

    # Use V4L2 for Ubuntu/Linux instead of DSHOW (Windows)
    cap = cv2.VideoCapture(cam_index, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if not cap.isOpened():
        raise RuntimeError("Could not open camera. Please check connections.")

    # ---- AprilTag detector ----
    detector = Detector(
        families="tag36h11",
        nthreads=2,
        quad_decimate=2.0,
        quad_sigma=0.0,
        refine_edges=1,
        decode_sharpening=0.25,
        debug=0
    )

    # ---- Pose parameters ----
    tag_size_m = 0.152  

    w, h = 1280, 720
    fx = fy = 900.0
    cx = w / 2.0
    cy = h / 2.0

    print("Camera initialized. Looking for AprilTags... (Press Ctrl+C to quit)")
    
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

            tags = detector.detect(
                gray,
                estimate_tag_pose=True,
                camera_params=(fx, fy, cx, cy),
                tag_size=tag_size_m
            )

            best = None
            if tags:
                best = max(tags, key=lambda t: t.decision_margin)

            if best is not None:
                tag_type = "tag36h11"
                tag_id = best.tag_id

                tx, ty, tz = best.pose_t.flatten().tolist()

                R = best.pose_R
                yaw = math.atan2(R[1,0], R[0,0])
                pitch = math.atan2(-R[2,0], math.sqrt(R[2,1]**2 + R[2,2]**2))
                roll = math.atan2(R[2,1], R[2,2])
                roll, pitch, yaw = map(math.degrees, (roll, pitch, yaw))

                # Print to SSH terminal instead of opening a video window
                print(f"ID: {tag_id} | X: {tx:.2f}m, Y: {ty:.2f}m, Z: {tz:.2f}m | Roll: {roll:.0f}, Pitch: {pitch:.0f}, Yaw: {yaw:.0f}")

    except KeyboardInterrupt:
        print("\nExiting script.")

    finally:
        cap.release()

if __name__ == "__main__":
    main()
