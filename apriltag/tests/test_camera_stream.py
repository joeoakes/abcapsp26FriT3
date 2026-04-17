import pytest
import cv2
import time

class TestCameraIntegration:

    def test_tc_063_camera_stream_read(self):
        """
        TC-063: Verify camera stream is read successfully by the AprilTag pipeline.
        Environment: Integration / Grey Box
        
        NOTE: This test requires a physical camera (webcam or robot camera) 
        to be connected to the device running the test.
        """
        # Act: Attempt to open the default camera (Index 0). 
        # If using an external USB cam, this might be 1 or 2.
        print("\nAttempting to connect to Camera 0...")
        cap = cv2.VideoCapture(0) 

        # Assert 1: Did the camera open successfully?
        assert cap.isOpened(), "CRITICAL FAILURE: Could not open the camera stream. Is it plugged in and not being used by another app (like Zoom)?"

        frames_captured = 0
        start_time = time.time()
        timeout_seconds = 5.0  # Give it 5 seconds to capture 15 frames

        # Act: Try to read 15 frames over a few seconds to ensure stability
        while frames_captured < 15 and (time.time() - start_time) < timeout_seconds:
            # Read a frame
            ret, frame = cap.read()
            
            if ret and frame is not None:
                # Assert 2: Ensure the frame actually contains data (not completely empty)
                assert frame.size > 0, "Captured an empty/corrupted frame."
                frames_captured += 1
            
            # Small delay to simulate the time it takes to process an AprilTag
            time.sleep(0.1) 

        # Cleanup: ALWAYS release the camera when done!
        cap.release()

        # Assert 3: Did we get all the frames before the timeout?
        assert frames_captured == 15, f"Stream unstable: Only captured {frames_captured}/15 frames before timeout."
        
        print("Camera stream stabilized and read successfully.")