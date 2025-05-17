import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import cv2
import argparse
import os
import pygame
import pygame.locals

def extract_pixel_array(image_path, grid_size=None, threshold=170, cell_threshold=0.15,
                        purple_boost=True, blob_detection=True, debug_output=False):
    """
    Extract a binary pixel array from an image containing glowing pixels.

    Args:
        image_path: Path to the input image
        grid_size: Size of the grid cells (if None, will attempt to detect automatically)
        threshold: Brightness threshold (0-255) to identify pixels (lower for purple glow)
        cell_threshold: Minimum ratio of bright pixels in a cell to consider it "on"
        purple_boost: Enhance purple components for better glow detection
        blob_detection: Use blob detection for more accurate pixel centers
        debug_output: Whether to save debug visualization images

    Returns:
        pixel_array: Binary numpy array where 1s represent detected pixels
        grid_size: The grid size used for the array
    """
    # Load image
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Could not load image: {image_path}")

    # Get dimensions
    height, width = img.shape[:2]

    # Create debug directory if needed
    if debug_output:
        os.makedirs("debug", exist_ok=True)

    # Process for better pixel detection
    if purple_boost:
        # Split into channels
        b, g, r = cv2.split(img)

        # Boost purple (high blue and red, low green)
        enhanced = cv2.addWeighted(b, 0.5, r, 0.5, 0) - g * 0.3
        enhanced = np.clip(enhanced, 0, 255).astype(np.uint8)

        if debug_output:
            cv2.imwrite("debug/enhanced_purple.png", enhanced)
    else:
        # Convert to grayscale if not using purple boost
        enhanced = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

    # Apply adaptive thresholding for better handling of gradients
    block_size = 35  # Size of pixel neighborhood for adaptive thresholding
    adaptive_threshold = cv2.adaptiveThreshold(enhanced, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
                                               cv2.THRESH_BINARY, block_size, -5)

    # Also apply normal thresholding as a baseline
    _, normal_threshold = cv2.threshold(enhanced, threshold, 255, cv2.THRESH_BINARY)

    # Combine both thresholds for robustness
    combined_threshold = cv2.bitwise_or(adaptive_threshold, normal_threshold)

    # Apply morphological operations to clean up the image
    kernel = np.ones((3, 3), np.uint8)
    cleaned = cv2.morphologyEx(combined_threshold, cv2.MORPH_OPEN, kernel)

    if debug_output:
        cv2.imwrite("debug/adaptive_threshold.png", adaptive_threshold)
        cv2.imwrite("debug/normal_threshold.png", normal_threshold)
        cv2.imwrite("debug/combined_threshold.png", combined_threshold)
        cv2.imwrite("debug/cleaned_threshold.png", cleaned)

    # If grid_size is not provided, try to estimate it
    if grid_size is None:
        if blob_detection:
            # Set up the blob detector parameters
            params = cv2.SimpleBlobDetector_Params()
            params.filterByArea = True
            params.minArea = 30
            params.maxArea = 10000
            params.filterByCircularity = False
            params.filterByConvexity = False
            params.filterByInertia = False
            params.minDistBetweenBlobs = 10

            # Create blob detector
            detector = cv2.SimpleBlobDetector_create(params)

            # Invert the image for blob detection since we're looking for white blobs
            inverted = cv2.bitwise_not(cleaned)

            # Detect blobs
            keypoints = detector.detect(inverted)

            if debug_output:
                # Draw keypoints on debug image
                blob_image = cv2.drawKeypoints(img, keypoints, np.array([]), (0, 255, 0),
                                               cv2.DRAW_MATCHES_FLAGS_DRAW_RICH_KEYPOINTS)
                cv2.imwrite("debug/blob_detection.png", blob_image)

            # Get blob centers
            centers = [(int(k.pt[0]), int(k.pt[1])) for k in keypoints]
        else:
            # Find contours instead if not using blob detection
            contours, _ = cv2.findContours(cleaned, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

            # Calculate contour centers
            centers = []
            for contour in contours:
                M = cv2.moments(contour)
                if M["m00"] > 0:
                    cx = int(M["m10"] / M["m00"])
                    cy = int(M["m01"] / M["m00"])
                    centers.append((cx, cy))

        if len(centers) >= 2:
            # Use Delaunay triangulation to find most common distances between points
            if len(centers) > 3:
                try:
                    # Convert centers to numpy array
                    points = np.array(centers)

                    # Create Delaunay triangulation
                    tri = cv2.Subdiv2D((0, 0, width, height))
                    for p in points:
                        tri.insert((float(p[0]), float(p[1])))

                    # Get triangulation edges
                    edge_list = []
                    edge_start = []

                    # Get edges from triangulation
                    for e in range(0, tri.getEdgeCount()):
                        org_pt = []
                        dst_pt = []
                        edge = tri.getEdge(e)
                        if edge[0]:
                            org_pt = tri.getVertex(edge[1])
                            dst_pt = tri.getVertex(edge[2])
                            edge_start.append(org_pt)
                            edge_list.append(np.sqrt((org_pt[0] - dst_pt[0]) ** 2 + (org_pt[1] - dst_pt[1]) ** 2))

                    # Find most common distance
                    edge_list = np.array(edge_list)

                    # Group similar distances (within 10% of each other)
                    edges_sorted = np.sort(edge_list)
                    groups = []
                    current_group = [edges_sorted[0]]

                    for i in range(1, len(edges_sorted)):
                        if edges_sorted[i] < current_group[0] * 1.1:
                            current_group.append(edges_sorted[i])
                        else:
                            groups.append(current_group)
                            current_group = [edges_sorted[i]]

                    if current_group:
                        groups.append(current_group)

                    # Find the largest group
                    largest_group = max(groups, key=len)
                    grid_size = int(np.mean(largest_group))

                    if debug_output:
                        # Draw triangulation on debug image
                        triangulation_img = img.copy()
                        for e in range(tri.getEdgeCount()):
                            org_pt = []
                            dst_pt = []
                            edge = tri.getEdge(e)
                            if edge[0]:
                                org_pt = tri.getVertex(edge[1])
                                dst_pt = tri.getVertex(edge[2])
                                org_pt = tuple(map(int, org_pt))
                                dst_pt = tuple(map(int, dst_pt))
                                cv2.line(triangulation_img, org_pt, dst_pt, (0, 255, 0), 1)
                        cv2.imwrite("debug/triangulation.png", triangulation_img)

                except Exception as e:
                    print(f"Triangulation failed: {e}. Falling back to distance calculation.")
                    # Calculate pairwise distances between centers as fallback
                    distances = []
                    for i in range(len(centers)):
                        nearest_dists = []
                        for j in range(len(centers)):
                            if i != j:
                                x1, y1 = centers[i]
                                x2, y2 = centers[j]
                                dist = np.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2)
                                nearest_dists.append(dist)
                        if nearest_dists:
                            # Use the minimum distance for each center
                            nearest_dists.sort()
                            distances.append(nearest_dists[0])

                    if distances:
                        # Use median of distances to be robust to outliers
                        grid_size = int(np.median(distances))
            else:
                # Calculate pairwise distances between centers
                distances = []
                for i in range(len(centers)):
                    for j in range(i + 1, len(centers)):
                        x1, y1 = centers[i]
                        x2, y2 = centers[j]
                        dist = np.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2)
                        distances.append(dist)

                if distances:
                    # Use median of distances to be robust to outliers
                    grid_size = int(np.median(distances))
                else:
                    # Default if we can't estimate properly
                    grid_size = min(width, height) // 10
        else:
            # Default if we can't estimate
            grid_size = min(width, height) // 10

    print(f"Using grid size: {grid_size} pixels")

    # Detect grid alignment
    # To handle slight rotation or stretching, try to find the actual grid
    # by looking for alignment patterns in the detected points

    # For simplicity, we'll use a hough line approach to detect potential grid lines
    # This helps with slight rotation
    if len(centers) > 5 and debug_output:
        alignment_img = np.zeros((height, width), dtype=np.uint8)
        for cx, cy in centers:
            cv2.circle(alignment_img, (cx, cy), 3, 255, -1)

        lines = cv2.HoughLines(alignment_img, 1, np.pi / 180, threshold=3)
        if lines is not None:
            alignment_debug = img.copy()
            for line in lines:
                rho, theta = line[0]
                a = np.cos(theta)
                b = np.sin(theta)
                x0 = a * rho
                y0 = b * rho
                x1 = int(x0 + 1000 * (-b))
                y1 = int(y0 + 1000 * (a))
                x2 = int(x0 - 1000 * (-b))
                y2 = int(y0 - 1000 * (a))
                cv2.line(alignment_debug, (x1, y1), (x2, y2), (0, 0, 255), 1)

            cv2.imwrite("debug/grid_alignment.png", alignment_debug)

    # Calculate grid dimensions
    grid_width = width // grid_size
    grid_height = height // grid_size

    # Create empty grid
    grid = np.zeros((grid_height, grid_width), dtype=int)

    # Create debug visualization if requested
    if debug_output:
        debug_img = img.copy()

    # Check each grid cell using a more sophisticated approach for purple glow
    for y in range(grid_height):
        for x in range(grid_width):
            # Extract the region
            cell_y_start = y * grid_size
            cell_y_end = min((y + 1) * grid_size, height)
            cell_x_start = x * grid_size
            cell_x_end = min((x + 1) * grid_size, width)

            # Get the cell as a rectangular region
            cell_rgb = img[cell_y_start:cell_y_end, cell_x_start:cell_x_end]

            if purple_boost:
                # Check for presence of purple glow (high in red and blue, low in green)
                b, g, r = cv2.split(cell_rgb)

                # Calculate purple score: high where blue and red are high but green is low
                purple_score = (b.astype(np.float32) + r.astype(np.float32)) / 2 - g.astype(np.float32) * 0.5
                purple_score = np.clip(purple_score, 0, 255)

                # Find bright areas in the normal cell too
                cell_gray = cv2.cvtColor(cell_rgb, cv2.COLOR_BGR2GRAY)

                # Combine brightness and purple score
                combined_score = (cell_gray.astype(np.float32) * 0.7 + purple_score * 0.3)

                # Check ratio of high-scoring pixels
                high_score_ratio = np.sum(combined_score > threshold) / combined_score.size

                # Also check for bright center - strong indicator of a pixel
                center_region = cell_gray[cell_gray.shape[0] // 4:3 * cell_gray.shape[0] // 4,
                                cell_gray.shape[1] // 4:3 * cell_gray.shape[1] // 4]
                center_brightness = np.mean(center_region)

                # Mark as a pixel if either condition is met
                if high_score_ratio > cell_threshold or center_brightness > threshold + 20:
                    grid[y, x] = 1
            else:
                # Just use grayscale approach if not boosting purple
                cell_gray = cv2.cvtColor(cell_rgb, cv2.COLOR_BGR2GRAY)
                bright_ratio = np.sum(cell_gray > threshold) / cell_gray.size
                if bright_ratio > cell_threshold:
                    grid[y, x] = 1

            # Add grid visualization for debugging
            if debug_output:
                color = (0, 255, 0) if grid[y, x] == 1 else (0, 0, 255)
                cv2.rectangle(debug_img,
                              (cell_x_start, cell_y_start),
                              (cell_x_end, cell_y_end),
                              color, 2)

    # Save debug visualizations if requested
    if debug_output:
        cv2.imwrite("debug/grid_detection.png", debug_img)

        # Create visualization of the detected grid
        grid_vis = np.zeros((grid_height * 10, grid_width * 10), dtype=np.uint8)
        for y in range(grid_height):
            for x in range(grid_width):
                if grid[y, x] == 1:
                    grid_vis[y * 10:(y + 1) * 10, x * 10:(x + 1) * 10] = 255
        cv2.imwrite("debug/detected_array.png", grid_vis)

    return grid, grid_size


def render_pixel_image(pixel_array, grid_size=30, pixel_size=None,
                       glow_radius=None, glow_color=(128, 0, 255),
                       background_color=(0, 0, 0), glow_intensity=0.8):
    """
    Render an image from a binary pixel array with glowing effect.

    Args:
        pixel_array: Binary numpy array where 1s represent pixels to draw
        grid_size: Size of each grid cell in the output image
        pixel_size: Size of each white pixel (default: grid_size * 0.6)
        glow_radius: Radius of the glow effect (default: grid_size * 0.8)
        glow_color: RGB color tuple for the glow
        background_color: RGB color tuple for the background
        glow_intensity: Intensity of the glow effect (0-1)

    Returns:
        PIL Image with the rendered result
    """
    # Set defaults based on grid_size if not provided
    if pixel_size is None:
        pixel_size = int(grid_size * 0.6)
    if glow_radius is None:
        glow_radius = int(grid_size * 0.8)

    # Calculate dimensions
    height, width = pixel_array.shape
    img_width = width * grid_size
    img_height = height * grid_size

    # Create a blank image with the specified background color
    bg_color_rgba = background_color + (255,)
    image = Image.new('RGBA', (img_width, img_height), color=bg_color_rgba)
    draw = ImageDraw.Draw(image)

    # Create a separate image for the glow effect
    glow_image = Image.new('RGBA', (img_width, img_height), color=(0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow_image)

    # Calculate alpha for glow based on intensity
    glow_alpha = int(255 * glow_intensity)
    glow_color_rgba = glow_color + (glow_alpha,)

    # Draw each pixel
    for y in range(height):
        for x in range(width):
            if pixel_array[y, x] == 1:
                # Calculate center position
                center_x = x * grid_size + grid_size // 2
                center_y = y * grid_size + grid_size // 2

                # Draw the glow (larger circle with color)
                glow_size = pixel_size + glow_radius
                glow_draw.ellipse(
                    [
                        center_x - glow_size,
                        center_y - glow_size,
                        center_x + glow_size,
                        center_y + glow_size
                    ],
                    fill=glow_color_rgba
                )

                # Draw the base white square
                half_size = pixel_size // 2
                draw.rectangle(
                    [
                        center_x - half_size,
                        center_y - half_size,
                        center_x + half_size,
                        center_y + half_size
                    ],
                    fill=(255, 255, 255, 255)  # Solid white
                )

    # Apply blur to the glow image
    glow_image = glow_image.filter(ImageFilter.GaussianBlur(radius=glow_radius / 2))

    # Composite: first the glow, then the white squares on top
    result = Image.new('RGBA', (img_width, img_height), color=bg_color_rgba)
    result.paste(glow_image, (0, 0), glow_image)
    result.paste(image, (0, 0), image)

    return result


def save_pixel_array(pixel_array, filename):
    """Save a pixel array to a .npy file"""
    np.save(filename, pixel_array)
    print(f"Saved pixel array to {filename}")


def load_pixel_array(filename):
    """Load a pixel array from a .npy file"""
    return np.load(filename)


def save_array_as_text(pixel_array, filename):
    """Save the pixel array as a human-readable text file"""
    with open(filename, 'w') as f:
        for row in pixel_array:
            f.write(''.join(['X' if cell == 1 else '.' for cell in row]) + '\n')
    print(f"Saved human-readable array to {filename}")


def load_array_from_text(filename):
    """Load a pixel array from a text file where 'X' or '#' = 1, anything else = 0"""
    with open(filename, 'r') as f:
        lines = f.readlines()

    # Create array
    height = len(lines)
    width = len(lines[0].strip())
    array = np.zeros((height, width), dtype=int)

    # Fill array
    for y, line in enumerate(lines):
        for x, char in enumerate(line.strip()):
            if char in ['X', '#', '1']:
                array[y, x] = 1

    return array


def edit_pixel_array(pixel_array, row, col, value=1):
    """Set a specific pixel in the array to the given value (1=on, 0=off)"""
    if 0 <= row < pixel_array.shape[0] and 0 <= col < pixel_array.shape[1]:
        pixel_array[row, col] = value
    else:
        print(f"Error: Position ({row}, {col}) is outside array dimensions {pixel_array.shape}")
    return pixel_array


def create_empty_pixel_array(height, width):
    """Create an empty pixel array of the specified dimensions"""
    return np.zeros((height, width), dtype=int)


def create_cat_face_array(height=15, width=20):
    """Create a simple cat face as an example"""
    array = np.zeros((height, width), dtype=int)

    # Draw face outline
    middle_w = width // 2
    middle_h = height // 2

    # Eyes
    eye_offset_x = width // 6
    eye_offset_y = height // 8
    array[middle_h - eye_offset_y, middle_w - eye_offset_x] = 1  # Left eye
    array[middle_h - eye_offset_y, middle_w + eye_offset_x] = 1  # Right eye

    # Eyebrows
    eyebrow_y = middle_h - eye_offset_y - 1
    array[eyebrow_y, middle_w - eye_offset_x - 1] = 1  # Left eyebrow start
    array[eyebrow_y, middle_w - eye_offset_x] = 1  # Left eyebrow middle
    array[eyebrow_y, middle_w - eye_offset_x + 1] = 1  # Left eyebrow end

    array[eyebrow_y, middle_w + eye_offset_x - 1] = 1  # Right eyebrow start
    array[eyebrow_y, middle_w + eye_offset_x] = 1  # Right eyebrow middle
    array[eyebrow_y, middle_w + eye_offset_x + 1] = 1  # Right eyebrow end

    # Nose
    array[middle_h, middle_w] = 1

    # Mouth
    mouth_y = middle_h + height // 8
    array[mouth_y, middle_w - 1] = 1
    array[mouth_y, middle_w] = 1
    array[mouth_y, middle_w + 1] = 1

    # Whiskers
    whisker_y = middle_h + height // 16
    whisker_x_offset = width // 4
    array[whisker_y, middle_w - whisker_x_offset] = 1  # Left whisker
    array[whisker_y, middle_w + whisker_x_offset] = 1  # Right whisker

    return array


def display_array_in_terminal(pixel_array):
    """Display the pixel array in the terminal using text characters"""
    for row in pixel_array:
        print(''.join(['X' if cell == 1 else '.' for cell in row]))


def create_vertical_symmetry(pixel_array, symmetry_x):
    """
    Creates a mirror image across the vertical axis at symmetry_x.
    Pixels on the symmetry line remain untouched.
    Pixels to the left of the line are mirrored to the right.
    """
    height, width = pixel_array.shape

    # Make sure the symmetry axis is within the grid
    if symmetry_x < 0 or symmetry_x >= width:
        return pixel_array

    # Create a copy of the current array
    result = pixel_array.copy()

    # For each row
    for y in range(height):
        # For each column to the left of the symmetry axis
        for x in range(symmetry_x):
            # Calculate the mirror position
            mirror_x = 2 * symmetry_x - x

            # Only if the mirror position is within bounds
            if mirror_x < width:
                # Copy the pixel value from left to right
                result[y, mirror_x] = pixel_array[y, x]

    return result


def draw_circle_on_array(pixel_array, center_x, center_y, radius, fill_value):
    """
    Draw a circle on the pixel array using the midpoint circle algorithm
    """
    height, width = pixel_array.shape

    # For each pixel in a bounding square around the circle
    for y in range(-radius, radius + 1):
        for x in range(-radius, radius + 1):
            # Calculate squared distance from center
            distance_squared = x * x + y * y

            # Determine if the pixel is within the circle
            is_on_perimeter = abs(distance_squared - radius * radius) < radius  # Approximate perimeter
            is_inside = distance_squared <= radius * radius

            # Get the actual array coordinates
            array_y = center_y + y
            array_x = center_x + x

            # Check if coordinates are within array bounds
            if 0 <= array_y < height and 0 <= array_x < width:
                if is_inside:
                    pixel_array[array_y, array_x] = fill_value

    return pixel_array


def show_circle_popup(screen):
    """
    Show a popup to get circle parameters
    Returns: (radius, fill_value)
    """
    popup_width, popup_height = 300, 200
    popup_x = (screen.get_width() - popup_width) // 2
    popup_y = (screen.get_height() - popup_height) // 2

    popup = pygame.Surface((popup_width, popup_height))
    popup.fill((50, 50, 50))
    pygame.draw.rect(popup, (100, 100, 100), (0, 0, popup_width, popup_height), 3)

    try:
        font = pygame.font.SysFont('Arial', 18)
    except:
        font = pygame.font.Font(None, 18)

    # Text elements
    title = font.render("Circle Parameters", True, (255, 255, 255))
    radius_text = font.render("Radius (pixels):", True, (255, 255, 255))
    fill_text = font.render("Fill (1) or Empty (0):", True, (255, 255, 255))

    # Input variables
    radius = "5"
    fill_value = "1"
    active_input = "radius"  # Which input field is active

    # Buttons
    ok_button = pygame.Rect(popup_width // 2 - 60, popup_height - 40, 50, 30)
    cancel_button = pygame.Rect(popup_width // 2 + 10, popup_height - 40, 50, 30)

    running = True
    result = None

    while running:
        # Draw popup
        popup.fill((50, 50, 50))
        pygame.draw.rect(popup, (100, 100, 100), (0, 0, popup_width, popup_height), 3)

        # Draw title
        popup.blit(title, (popup_width // 2 - title.get_width() // 2, 10))

        # Draw radius input
        popup.blit(radius_text, (20, 50))
        radius_rect = pygame.Rect(180, 50, 100, 24)
        pygame.draw.rect(popup, (255, 255, 255), radius_rect, 1)
        if active_input == "radius":
            pygame.draw.rect(popup, (100, 100, 255), radius_rect, 2)
        radius_surf = font.render(radius, True, (255, 255, 255))
        popup.blit(radius_surf, (185, 53))

        # Draw fill value input
        popup.blit(fill_text, (20, 90))
        fill_rect = pygame.Rect(180, 90, 100, 24)
        pygame.draw.rect(popup, (255, 255, 255), fill_rect, 1)
        if active_input == "fill":
            pygame.draw.rect(popup, (100, 100, 255), fill_rect, 2)
        fill_surf = font.render(fill_value, True, (255, 255, 255))
        popup.blit(fill_surf, (185, 93))

        # Draw buttons
        pygame.draw.rect(popup, (100, 100, 200), ok_button)
        pygame.draw.rect(popup, (200, 100, 100), cancel_button)
        ok_text = font.render("OK", True, (255, 255, 255))
        cancel_text = font.render("Cancel", True, (255, 255, 255))
        popup.blit(ok_text, (ok_button.x + 15, ok_button.y + 8))
        popup.blit(cancel_text, (cancel_button.x + 5, cancel_button.y + 8))

        screen.blit(popup, (popup_x, popup_y))
        pygame.display.flip()

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                result = None

            elif event.type == pygame.MOUSEBUTTONDOWN:
                mouse_pos = event.pos
                # Adjust for popup position
                rel_x, rel_y = mouse_pos[0] - popup_x, mouse_pos[1] - popup_y

                # Check which input is clicked
                if radius_rect.collidepoint((rel_x, rel_y)):
                    active_input = "radius"
                elif fill_rect.collidepoint((rel_x, rel_y)):
                    active_input = "fill"
                # Check if buttons are clicked
                elif ok_button.collidepoint((rel_x, rel_y)):
                    try:
                        r = int(radius)
                        f = int(fill_value)
                        if r > 0 and (f == 0 or f == 1):
                            result = (r, f)
                            running = False
                    except ValueError:
                        pass
                elif cancel_button.collidepoint((rel_x, rel_y)):
                    running = False
                    result = None

            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_TAB:
                    # Switch between inputs
                    active_input = "fill" if active_input == "radius" else "radius"
                elif event.key == pygame.K_RETURN:
                    try:
                        r = int(radius)
                        f = int(fill_value)
                        if r > 0 and (f == 0 or f == 1):
                            result = (r, f)
                            running = False
                    except ValueError:
                        pass
                elif event.key == pygame.K_ESCAPE:
                    running = False
                    result = None
                elif event.key == pygame.K_BACKSPACE:
                    if active_input == "radius":
                        radius = radius[:-1]
                    else:
                        fill_value = fill_value[:-1]
                else:
                    if event.unicode.isdigit():
                        if active_input == "radius":
                            radius += event.unicode
                        else:
                            # For fill, only allow 0 or 1
                            if event.unicode in ['0', '1']:
                                fill_value = event.unicode

    return result


def create_interactive_editor(pixel_array, background_image_path=None):
    """
    Create an interactive pixel editor using Pygame

    Args:
        pixel_array: The initial pixel array to edit
        background_image_path: Optional path to a background image

    Returns:
        The edited pixel array
    """
    try:
        import pygame
    except ImportError:
        print("Pygame is not installed. Please install it with 'pip install pygame'")
        return pixel_array

    # Initialize pygame
    pygame.init()

    # Get array dimensions
    height, width = pixel_array.shape

    # Initial settings
    cell_size = 40
    max_cell_size = 100
    min_cell_size = 5
    padding = 50  # Extra space around the grid

    # Set up display
    screen_width = width * cell_size + padding * 2
    screen_height = height * cell_size + padding * 2
    screen = pygame.display.set_mode((screen_width, screen_height), pygame.RESIZABLE)
    pygame.display.set_caption("Pixel Editor")

    # Colors
    bg_color = (30, 30, 30)
    grid_color = (100, 100, 100)
    grid_color2 = (100, 0, 0)
    pixel_color = (200, 100, 255)
    highlight_color = (0, 255, 0)
    text_color = (255, 255, 255)
    pixel_outline_color = (255, 255, 255)

    # Load background image if provided
    background_img = None
    if background_image_path:
        try:
            background_img = pygame.image.load(background_image_path)
            print(f"Loaded background image: {background_image_path}")
        except Exception as e:
            print(f"Error loading background image: {e}")

    # Editing state
    panning = False
    pan_start = (0, 0)
    offset_x, offset_y = padding, padding
    background_opacity = 0.3
    grid_visible = True

    # Clone the original array for undo
    original_array = pixel_array.copy()

    # Set up fonts
    try:
        font = pygame.font.SysFont('Arial', 12)
    except:
        font = pygame.font.Font(None, 12)  # Fallback to default font

    # Helper function to convert screen coordinates to grid coordinates
    def screen_to_grid(x, y):
        grid_x = (x - offset_x) // cell_size
        grid_y = (y - offset_y) // cell_size
        return grid_x, grid_y

    # Helper function to convert grid coordinates to screen coordinates
    def grid_to_screen(grid_x, grid_y):
        screen_x = grid_x * cell_size + offset_x
        screen_y = grid_y * cell_size + offset_y
        return screen_x, screen_y

    # Main editing loop
    clock = pygame.time.Clock()
    running = True
    right_mouse_down = False
    painting_value = 0

    while running:
        # Handle events
        for event in pygame.event.get():
            if event == pygame.QUIT:
                running = False

            # Keyboard events
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_g:
                    grid_visible = not grid_visible
                elif event.key == pygame.K_r:
                    pixel_array = original_array.copy()
                elif event.key == pygame.K_c:
                    # Get mouse position for circle center
                    mouse_x, mouse_y = pygame.mouse.get_pos()
                    grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)

                    # Show popup to get circle parameters
                    circle_params = show_circle_popup(screen)

                    # Draw circle if parameters were provided
                    if circle_params:
                        radius, fill_value = circle_params
                        pixel_array = draw_circle_on_array(pixel_array, grid_x, grid_y, radius, fill_value)
                elif event.key == pygame.K_v:
                    # Get mouse position for symmetry axis
                    mouse_x, mouse_y = pygame.mouse.get_pos()
                    grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)

                    # Create vertical symmetry
                    pixel_array = create_vertical_symmetry(pixel_array, grid_x)
                elif event.key == pygame.K_PLUS or event.key == pygame.K_EQUALS:
                    old_cell_size = cell_size
                    cell_size = min(cell_size + 5, max_cell_size)
                    # Adjust offset to keep center point
                    center_x = screen_width // 2
                    center_y = screen_height // 2
                    grid_x, grid_y = screen_to_grid(center_x, center_y)
                    offset_x = center_x - grid_x * cell_size
                    offset_y = center_y - grid_y * cell_size
                elif event.key == pygame.K_MINUS:
                    old_cell_size = cell_size
                    cell_size = max(cell_size - 5, min_cell_size)
                    # Adjust offset to keep center point
                    center_x = screen_width // 2
                    center_y = screen_height // 2
                    grid_x, grid_y = screen_to_grid(center_x, center_y)
                    offset_x = center_x - grid_x * cell_size
                    offset_y = center_y - grid_y * cell_size

            # Window resize
            elif event.type == pygame.VIDEORESIZE:
                screen_width, screen_height = event.size
                screen = pygame.display.set_mode((screen_width, screen_height), pygame.RESIZABLE)

            # Mouse button events
            elif event.type == pygame.MOUSEBUTTONDOWN:
                if event.button == 1:  # Left mouse button - pan
                    panning = True
                    pan_start = event.pos
                elif event.button == 3:  # Right mouse button - toggle pixel
                    right_mouse_down = True  # Add this line
                    mouse_x, mouse_y = event.pos
                    grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)

                    if 0 <= grid_x < width and 0 <= grid_y < height:
                        # Determine painting value based on current pixel state
                        current_value = pixel_array[grid_y, grid_x]
                        painting_value = 1 - current_value  # If pixel is 0, we'll paint 1; if it's 1, we'll paint 0

                        # Apply the painting value to the clicked pixel
                        pixel_array[grid_y, grid_x] = painting_value
                elif event.button == 4:  # Mouse wheel up
                    if pygame.key.get_mods() & pygame.KMOD_SHIFT and background_img:
                        # Shift + wheel: adjust background opacity
                        background_opacity = min(1.0, background_opacity + 0.05)
                    else:
                        # Wheel only: zoom in
                        mouse_x, mouse_y = pygame.mouse.get_pos()
                        grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)

                        old_cell_size = cell_size
                        cell_size = min(cell_size + 2, max_cell_size)

                        # Adjust offset to zoom toward mouse cursor
                        offset_x = mouse_x - grid_x * cell_size
                        offset_y = mouse_y - grid_y * cell_size
                elif event.button == 5:  # Mouse wheel down
                    if pygame.key.get_mods() & pygame.KMOD_SHIFT and background_img:
                        # Shift + wheel: adjust background opacity
                        background_opacity = max(0.0, background_opacity - 0.05)
                    else:
                        # Wheel only: zoom out
                        mouse_x, mouse_y = pygame.mouse.get_pos()
                        grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)

                        old_cell_size = cell_size
                        cell_size = max(cell_size - 2, min_cell_size)

                        # Adjust offset to zoom toward mouse cursor
                        offset_x = mouse_x - grid_x * cell_size
                        offset_y = mouse_y - grid_y * cell_size

            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:  # Left mouse button release
                    panning = False
                elif event.button == 3:  # Right mouse button release
                    right_mouse_down = False

            elif event.type == pygame.MOUSEMOTION:
                if panning:
                    mouse_x, mouse_y = event.pos
                    dx = mouse_x - pan_start[0]
                    dy = mouse_y - pan_start[1]
                    offset_x += dx
                    offset_y += dy
                    pan_start = (mouse_x, mouse_y)
                elif right_mouse_down:
                    mouse_x, mouse_y = event.pos
                    grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)
                    if 0 <= grid_x < width and 0 <= grid_y < height:
                        pixel_array[grid_y, grid_x] = painting_value

        # Clear the screen
        screen.fill(bg_color)

        # Calculate visible grid area
        start_x = max(0, -offset_x // cell_size)
        start_y = max(0, -offset_y // cell_size)
        end_x = min(width, (screen_width - offset_x + cell_size - 1) // cell_size)
        end_y = min(height, (screen_height - offset_y + cell_size - 1) // cell_size)

        # Draw background image if provided
        if background_img and background_opacity > 0:
            # Scale background to match grid size
            scaled_img = pygame.transform.scale(background_img,
                                                (width * cell_size, height * cell_size))

            # Create a transparent surface
            transparent_bg = pygame.Surface((width * cell_size, height * cell_size),
                                            pygame.SRCALPHA)
            transparent_bg.fill((255, 255, 255, int(255 * background_opacity)))

            # Blit the image onto the transparent surface with alpha
            scaled_img.blit(transparent_bg, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)

            # Blit the result to the screen
            screen.blit(scaled_img, (offset_x, offset_y))

        if grid_visible:
            mouse_x, mouse_y = pygame.mouse.get_pos()
            grid_x, grid_y = screen_to_grid(mouse_x, mouse_y)

            # Highlight the entire column where the mouse is
            for row in range(screen_height):
                col_screen_x, col_screen_y = grid_to_screen(grid_x, row)
                pygame.draw.rect(screen, highlight_color,
                                 (col_screen_x, col_screen_y, cell_size, cell_size))

            # Highlight the entire row where the mouse is
            for col in range(screen_width):
                row_screen_x, row_screen_y = grid_to_screen(col, grid_y)
                pygame.draw.rect(screen, highlight_color,
                                 (row_screen_x, row_screen_y, cell_size, cell_size))

        # Draw active pixels
        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                if pixel_array[y, x] == 1:
                    screen_x, screen_y = grid_to_screen(x, y)

                    # Draw filled rectangle
                    pygame.draw.rect(screen, pixel_color,
                                     (screen_x, screen_y, cell_size, cell_size))

                    # Draw pixel outline for visibility
                    pygame.draw.rect(screen, pixel_outline_color,
                                     (screen_x, screen_y, cell_size, cell_size), 1)

        # Draw grid if enabled
        if grid_visible:

            for x in range(start_x, end_x + 1):
                screen_x = x * cell_size + offset_x
                pygame.draw.line(screen, grid_color if x % 2 == 0 else grid_color2,
                                 (screen_x, offset_y),
                                 (screen_x, end_y * cell_size + offset_y))

            for y in range(start_y, end_y + 1):
                screen_y = y * cell_size + offset_y
                pygame.draw.line(screen, grid_color if y % 2 == 0 else grid_color2,
                                 (offset_x, screen_y),
                                 (end_x * cell_size + offset_x, screen_y))

        # Draw coordinate indicators for visible grid corners
        if grid_visible and cell_size >= 20:
            for y in range(start_y, end_y):
                for x in range(start_x, end_x):
                    screen_x, screen_y = grid_to_screen(x, y)
                    coord_text = f"{y},{x}"
                    text_surf = font.render(coord_text, True, (200, 200, 200))
                    screen.blit(text_surf, (screen_x + 5, screen_y + 5))

        # Draw instructions
        instructions = [
            "Controls:",
            f"Left click + drag: Pan",
            f"Right click + drag: Toggle pixels",
            f"C: Draw circle",
            f"V: Create vertical symmetry at mouse position",
            f"Mouse wheel: Zoom in/out",
            f"Shift + wheel: Adjust background opacity",
            f"G: Toggle grid",
            f"R: Reset",
            f"ESC: Save and exit",
            f"",
            f"Cell size: {cell_size}px",
            f"Background opacity: {background_opacity:.2f}"
        ]

        for i, text in enumerate(instructions):
            text_surf = font.render(text, True, text_color)
            screen.blit(text_surf, (10, 10 + i * 20))

        # Draw current array dimensions
        dim_text = f"Array: {height}x{width}"
        dim_surf = font.render(dim_text, True, text_color)
        screen.blit(dim_surf, (screen_width - dim_surf.get_width() - 10, 10))

        # Update the display
        pygame.display.flip()
        clock.tick(60)  # Limit to 60 FPS

    # Clean up
    pygame.quit()
    return pixel_array


def main():
    parser = argparse.ArgumentParser(description='Create, edit, and render pixel art with glow effect')

    # Input/output options
    parser.add_argument('--input', '-i', help='Input image path to analyze')
    parser.add_argument('--output', '-o', default='textures/eyes_meow.png', help='Output image path')
    parser.add_argument('--save-array', '-s', default='npy/eyes_meow2.npy', help='Path to save the pixel array')
    parser.add_argument('--load-array', '-l', help='Path to load an existing pixel array', default='npy/eyes_meow.npy')
    parser.add_argument('--example', '-e', action='store_true', help='Create an example cat face')

    # Analysis options
    parser.add_argument('--grid-size', '-g', type=int, help='Grid size to use (default: auto-detect)')
    parser.add_argument('--threshold', '-t', type=int, default=170,
                        help='Brightness threshold (0-255, lower value for purple glow)')
    parser.add_argument('--cell-threshold', '-c', type=float, default=0.15,
                        help='Ratio of bright pixels needed in a cell')
    parser.add_argument('--purple-boost', '-pb', action='store_true', default=True,
                        help='Enhance purple colors for better glow detection')
    parser.add_argument('--blob-detection', '-bd', action='store_true', default=True,
                        help='Use blob detection for more accurate pixel centers')
    parser.add_argument('--debug', '-d', action='store_true', help='Save debug images')

    # Rendering options
    parser.add_argument('--pixel-size', '-p', type=int, help='Size of each pixel')
    parser.add_argument('--glow-radius', '-r', type=int, help='Radius of the glow effect')
    parser.add_argument('--glow-color', help='Glow color (R,G,B format)', default='128,0,255')
    parser.add_argument('--glow-intensity', type=float, default=0.8, help='Intensity of glow (0-1)')
    parser.add_argument('--background', default='0,0,0', help='Background color (R,G,B format)')

    # Editor options
    parser.add_argument('--visual-edit', '-v', action='store_true', help='Open visual interactive editor', default=True)
    parser.add_argument('--text-edit', '-x', action='store_true', help='Open text-based editor')
    parser.add_argument('--text-format', action='store_true',
                        help='Save/load array in human-readable text format')
    parser.add_argument('--background-image', '-bi', help='Background image for editor')
    parser.add_argument('--use-pygame', action='store_true', default=True,
                        help='Use Pygame for the visual editor (default: True)')

    args = parser.parse_args()

    # Parse colors
    glow_color = tuple(map(int, args.glow_color.split(',')) if args.glow_color else (128, 0, 255))
    bg_color = tuple(map(int, args.background.split(',')))

    # Get pixel array (from input image, example, or loaded file)
    if args.load_array:
        print(f"Loading pixel array from: {args.load_array}")
        if args.text_format:
            pixel_array = load_array_from_text(args.load_array)
        else:
            pixel_array = load_pixel_array(args.load_array)
        grid_size = args.grid_size or 30
    elif args.input:
        print(f"Analyzing image: {args.input}")
        pixel_array, grid_size = extract_pixel_array(
            args.input,
            grid_size=args.grid_size,
            threshold=args.threshold,
            cell_threshold=args.cell_threshold,
            purple_boost=args.purple_boost,
            blob_detection=args.blob_detection,
            debug_output=args.debug
        )
    elif args.example:
        print("Creating example cat face")
        grid_size = args.grid_size or 30
        pixel_array = create_cat_face_array()
    else:
        print("No input specified. Creating empty array.")
        grid_size = args.grid_size or 30
        pixel_array = create_empty_pixel_array(15, 20)

    # Show the array in the terminal
    print("\nCurrent pixel array:")
    display_array_in_terminal(pixel_array)

    # Interactive visual editor if requested
    if args.visual_edit:
        pixel_array = create_interactive_editor(pixel_array, args.background_image)

    # Text-based editor if requested
    if args.text_edit:
        print("\nText-Based Editor")
        print("----------------")
        print("Enter commands: 'set row col', 'clear row col', 'toggle row col', 'done'")

        while True:
            cmd = input("> ").strip().lower()

            if cmd == 'done':
                break

            parts = cmd.split()

            if len(parts) >= 3:
                action = parts[0]
                try:
                    row = int(parts[1])
                    col = int(parts[2])

                    if action == 'set':
                        pixel_array = edit_pixel_array(pixel_array, row, col, 1)
                    elif action == 'clear':
                        pixel_array = edit_pixel_array(pixel_array, row, col, 0)
                    elif action == 'toggle':
                        current = pixel_array[row, col] if 0 <= row < pixel_array.shape[0] and 0 <= col < pixel_array.shape[1] else 0
                        pixel_array = edit_pixel_array(pixel_array, row, col, 1 - current)

                    # Update display after each change
                    print("\nUpdated array:")
                    display_array_in_terminal(pixel_array)
                except ValueError:
                    print("Invalid row/col. Use integers.")
                except IndexError:
                    print(f"Position out of bounds. Array dimensions: {pixel_array.shape}")
            else:
                print("Invalid command format. Use: 'set row col', 'clear row col', or 'done'")

    # Save the array
    if args.save_array:
        if args.text_format:
            save_array_as_text(pixel_array, args.save_array + '.txt' if not args.save_array.endswith('.txt') else args.save_array)
        else:
            save_pixel_array(pixel_array, args.save_array)

    # Render the image
    result = render_pixel_image(
        pixel_array,
        grid_size=grid_size,
        pixel_size=args.pixel_size,
        glow_radius=args.glow_radius,
        glow_color=glow_color,
        background_color=bg_color,
        glow_intensity=args.glow_intensity
    )

    # Save the rendered image
    result.save(args.output)
    print(f"Saved rendered image to: {args.output}")
    print("\nDone!")


if __name__ == "__main__":
    main()
