import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import cv2
import argparse


def extract_pixel_array(image_path, grid_size=None, threshold=200, cell_threshold=0.3, debug_output=False):
    """
    Extract a binary pixel array from an image containing glowing pixels.

    Args:
        image_path: Path to the input image
        grid_size: Size of the grid cells (if None, will attempt to detect automatically)
        threshold: Brightness threshold (0-255) to identify pixels
        cell_threshold: Minimum ratio of bright pixels in a cell to consider it "on"
        debug_output: Whether to save debug visualization images

    Returns:
        pixel_array: Binary numpy array where 1s represent detected pixels
        grid_size: The grid size used for the array
    """
    # Load image
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Could not load image: {image_path}")

    # Convert to grayscale
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

    # Get dimensions
    height, width = gray.shape

    # If grid_size is not provided, try to estimate it
    if grid_size is None:
        # Apply threshold to get binary image
        _, thresh = cv2.threshold(gray, threshold, 255, cv2.THRESH_BINARY)

        if debug_output:
            cv2.imwrite("debug_threshold.png", thresh)

        # Find contours of bright areas
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        # Calculate distances between contour centers
        centers = []
        for contour in contours:
            M = cv2.moments(contour)
            if M["m00"] > 0:
                cx = int(M["m10"] / M["m00"])
                cy = int(M["m01"] / M["m00"])
                centers.append((cx, cy))

        if len(centers) >= 2:
            # Calculate pairwise distances between centers
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
                # Default if we can't estimate properly
                grid_size = min(width, height) // 10
        else:
            # Default if we can't estimate
            grid_size = min(width, height) // 10

    print(f"Using grid size: {grid_size} pixels")

    # Calculate grid dimensions
    grid_width = width // grid_size
    grid_height = height // grid_size

    # Create empty grid
    grid = np.zeros((grid_height, grid_width), dtype=int)

    # Create debug visualization if requested
    if debug_output:
        debug_img = img.copy()

    # Check each grid cell
    for y in range(grid_height):
        for x in range(grid_width):
            # Extract the region
            cell_y_start = y * grid_size
            cell_y_end = min((y + 1) * grid_size, height)
            cell_x_start = x * grid_size
            cell_x_end = min((x + 1) * grid_size, width)

            cell = gray[cell_y_start:cell_y_end, cell_x_start:cell_x_end]

            # Check if this cell contains enough bright pixels
            bright_ratio = np.sum(cell > threshold) / cell.size
            if bright_ratio > cell_threshold:
                grid[y, x] = 1

                # Add grid visualization for debugging
                if debug_output:
                    cv2.rectangle(debug_img,
                                  (cell_x_start, cell_y_start),
                                  (cell_x_end, cell_y_end),
                                  (0, 255, 0), 2)

    # Save debug visualizations if requested
    if debug_output:
        cv2.imwrite("debug_grid_detection.png", debug_img)

        # Create visualization of the detected grid
        grid_vis = np.zeros((grid_height * 10, grid_width * 10), dtype=np.uint8)
        for y in range(grid_height):
            for x in range(grid_width):
                if grid[y, x] == 1:
                    grid_vis[y * 10:(y + 1) * 10, x * 10:(x + 1) * 10] = 255
        cv2.imwrite("debug_detected_array.png", grid_vis)

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


def create_interactive_editor(pixel_array):
    """Create a simple visual interactive editor"""
    height, width = pixel_array.shape
    is_editing = True

    # Create a display window
    cell_size = 30
    window_height = height * cell_size
    window_width = width * cell_size
    editor = np.zeros((window_height, window_width, 3), dtype=np.uint8)

    # Make a copy of the original array for undo
    original_array = pixel_array.copy()

    def refresh_display():
        # Clear editor
        editor.fill(0)



        # Draw active cells
        for y in range(height):
            for x in range(width):
                if pixel_array[y, x] == 1:
                    top_left = (x * cell_size, y * cell_size)
                    bottom_right = ((x + 1) * cell_size, (y + 1) * cell_size)
                    cv2.rectangle(editor, top_left, bottom_right, (200, 100, 255), -1)

        # Draw grid
        for i in range(height + 1):
            y = i * cell_size
            cv2.line(editor, (0, y), (window_width, y), (50, 50, 50), 1)
        for i in range(width + 1):
            x = i * cell_size
            cv2.line(editor, (x, 0), (x, window_height), (50, 50, 50), 1)

        # Add instructions
        cv2.putText(editor, "Left click: toggle pixel", (10, window_height - 60),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
        cv2.putText(editor, "Right click: clear pixel", (10, window_height - 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
        cv2.putText(editor, "ESC: save and exit", (10, window_height - 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

        cv2.imshow("Pixel Editor", editor)

    def mouse_callback(event, x, y, flags, param):
        nonlocal pixel_array

        if event == cv2.EVENT_LBUTTONDOWN or event == cv2.EVENT_RBUTTONDOWN:
            # Convert coordinates to grid position
            grid_x = x // cell_size
            grid_y = y // cell_size

            # Make sure within bounds
            if 0 <= grid_x < width and 0 <= grid_y < height:
                if event == cv2.EVENT_LBUTTONDOWN:
                    # Toggle the pixel
                    pixel_array[grid_y, grid_x] = 1 - pixel_array[grid_y, grid_x]
                elif event == cv2.EVENT_RBUTTONDOWN:
                    # Clear the pixel
                    pixel_array[grid_y, grid_x] = 0

                refresh_display()

    # Setup the window and callback
    cv2.namedWindow("Pixel Editor")
    cv2.setMouseCallback("Pixel Editor", mouse_callback)

    print("\nVisual Pixel Editor")
    print("-----------------")
    print("Left click: Toggle pixel on/off")
    print("Right click: Clear pixel")
    print("ESC: Save and exit")

    # Main editing loop
    refresh_display()
    while is_editing:
        key = cv2.waitKey(100) & 0xFF

        if key == 27:  # ESC key - save and exit
            is_editing = False
        elif key == ord('r'):  # 'r' key - reset to original
            pixel_array = original_array.copy()
            refresh_display()

    cv2.destroyAllWindows()
    return pixel_array


def main():
    parser = argparse.ArgumentParser(description='Create, edit, and render pixel art with glow effect')

    # Input/output options
    parser.add_argument('--input', '-i', help='Input image path to analyze')
    parser.add_argument('--output', '-o', default='textures/pixel_kerfur.png', help='Output image path')
    parser.add_argument('--save-array', '-s', default='npy/pixel_kerfur.npy', help='Path to save the pixel array')
    parser.add_argument('--load-array', '-l', help='Path to load an existing pixel array', default='npy/eyes_opened_empty.npy')
    parser.add_argument('--example', '-e', action='store_true', help='Create an example cat face')

    # Analysis options
    parser.add_argument('--grid-size', '-g', type=int, help='Grid size to use (default: auto-detect)')
    parser.add_argument('--threshold', '-t', type=int, default=175, help='Brightness threshold (0-255)')
    parser.add_argument('--cell-threshold', '-c', type=float, default=0.3,
                        help='Ratio of bright pixels needed in a cell')
    parser.add_argument('--debug', '-d', action='store_true', help='Save debug images')

    # Rendering options
    parser.add_argument('--pixel-size', '-p', type=int, help='Size of each pixel')
    parser.add_argument('--glow-radius', '-r', type=int, help='Radius of the glow effect')
    parser.add_argument('--glow-color', help='Glow color (R,G,B format)')
    parser.add_argument('--glow-intensity', type=float, default=0.8, help='Intensity of glow (0-1)')
    parser.add_argument('--background', default='0,0,0', help='Background color (R,G,B format)')

    # Editor options
    parser.add_argument('--visual-edit', '-v', action='store_true', help='Open visual interactive editor', default=True)
    parser.add_argument('--text-edit', '-x', action='store_true', help='Open text-based editor')
    parser.add_argument('--text-format', action='store_true',
                        help='Save/load array in human-readable text format')

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
        if cv2.__version__:
            pixel_array = create_interactive_editor(pixel_array)
        else:
            print("OpenCV not properly installed. Visual editor not available.")

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
