import os
import json
import struct

import pygame
import time


# ============================================================
# Configuration
# ============================================================

PATTERN_DIR = os.path.join(os.getcwd(), "patterns")

PROJECT_FILE = "texture.json"
EXPORT_DIR = "exports"

CANVAS_SIZE = 768
MAX_PATTERNS = 128

WINDOW_WIDTH = 3000
WINDOW_HEIGHT = 1200

PATTERN_PANEL_WIDTH = 300
INFO_PANEL_WIDTH = 280

DEFAULT_ZOOM = 3
MIN_ZOOM = 1
MAX_ZOOM = 6

SUPPORTED_EXTENSIONS = (
    ".png",
    ".jpg",
    ".jpeg",
    ".bmp",
    ".tga",
    ".webp",
)


# ============================================================
# Colors
# ============================================================

BG = (30, 30, 33)

PANEL = (43, 43, 47)
PANEL_DARK = (34, 34, 37)

TEXT = (225, 225, 225)
TEXT_DIM = (145, 145, 150)

ACCENT = (80, 150, 255)
ACCENT_DARK = (55, 105, 180)

GREEN = (100, 210, 130)
RED = (225, 90, 90)

GRID = (58, 58, 63)
TILE_GRID = (255, 255, 255)
BORDER = (100, 100, 105)

CHECKER_A = (54, 54, 58)
CHECKER_B = (45, 45, 49)


# ============================================================
# Helpers
# ============================================================

def is_white(color):
    """
    White pixels in pattern images are ignored.
    """

    return (
        color.r == 255
        and color.g == 255
        and color.b == 255
    )


# ============================================================
# Placement
# ============================================================

class Placement:
    """
    One 24-bit placement record.

    Bit layout:

        0 .. 9    X, 10 bits required
        10 .. 19  Y, 10 bits required
        20        H mirror
        21        V mirror
        22        D mirror (only mirror diagonally)
        23        swap black for R

    The pattern number is NOT stored here.

    It is implied by the pattern whose placement list
    contains this record.
    """

    def __init__(
        self,
        x=0,
        y=0,
        hmirror=False,
        vmirror=False,
        dmirror=False,
        red=False,
    ):
        self.x = max(0, min(CANVAS_SIZE-1, int(x)))
        self.y = max(0, min(CANVAS_SIZE-1, int(y)))

        self.hmirror = bool(hmirror)
        self.vmirror = bool(vmirror)
        self.dmirror = bool(dmirror)
        self.red = bool(red)

    # --------------------------------------------------------

    def pack(self):
        """
        Return the 32-bit hardware representation.
        """

        value = 0

        value |= self.x & 0x3FF
        value |= (self.y & 0x3FF) << 10

        if self.hmirror:
            value |= 1 << 20

        if self.vmirror:
            value |= 1 << 21

        if self.dmirror:
            value |= 1 << 22

        if self.red:
            value |= 1 << 23

        return value

    # --------------------------------------------------------

    @classmethod
    def unpack(cls, value):
        """
        Construct a Placement from a 24-bit record.
        """

        return cls(
            x=value & 0x7F,
            y=(value >> 7) & 0x7F,
            hmirror=bool(value & (1<<20)),
            vmirror=bool(value & (1<<21)),
            dmirror=bool(value & (1<<22)),
            red=bool(value & (1<<23))
        )

    # --------------------------------------------------------

    def to_dict(self):
        return {
            "x": self.x,
            "y": self.y,
            "hmirror": self.hmirror,
            "vmirror": self.vmirror,
            "dmirror": self.dmirror,
            "red": self.red,
        }

    # --------------------------------------------------------

    @classmethod
    def from_dict(cls, data):
        return cls(
            x=data.get("x", 0),
            y=data.get("y", 0),
            hmirror=data.get("hmirror", False),
            vmirror=data.get("vmirror", False),
            dmirror=data.get("dmirror", False),
            red=data.get("red", False)
        )


# ============================================================
# Pattern
# ============================================================

class Pattern:
    """
    A source pattern image and its placement records.
    """

    def __init__(self, index, filename):

        self.index = index
        self.filename = filename
        self.name = os.path.basename(filename)

        self.image = pygame.image.load(
            filename
        ).convert_alpha()

        self.width = self.image.get_width()
        self.height = self.image.get_height()

        self.placements = []

        self.thumbnail = self.make_thumbnail(
            100,
            76
        )

    # --------------------------------------------------------

    def make_thumbnail(self, width, height):

        result = pygame.Surface(
            (width, height),
            pygame.SRCALPHA
        )

        result.fill(
            (25, 25, 28, 255)
        )

        scale = min(
            width / self.width,
            height / self.height
        )

        new_width = max(
            1,
            int(self.width * scale)
        )

        new_height = max(
            1,
            int(self.height * scale)
        )

        image = pygame.transform.scale(
            self.image,
            (new_width, new_height)
        )

        x = (width - new_width) // 2
        y = (height - new_height) // 2

        result.blit(
            image,
            (x, y)
        )

        return result


# ============================================================
# Button
# ============================================================

class Button:

    def __init__(self, rect, text):

        self.rect = rect
        self.text = text

    # --------------------------------------------------------

    def draw(self, surface, font, mouse_pos):

        hovered = self.rect.collidepoint(
            mouse_pos
        )

        color = (
            ACCENT_DARK
            if hovered
            else PANEL_DARK
        )

        pygame.draw.rect(
            surface,
            color,
            self.rect,
            border_radius=3
        )

        pygame.draw.rect(
            surface,
            BORDER,
            self.rect,
            1,
            border_radius=3
        )

        text_surface = font.render(
            self.text,
            True,
            TEXT
        )

        text_rect = text_surface.get_rect(
            center=self.rect.center
        )

        surface.blit(
            text_surface,
            text_rect
        )

    # --------------------------------------------------------

    def clicked(self, event):

        return (
            event.type == pygame.MOUSEBUTTONDOWN
            and event.button == 1
            and self.rect.collidepoint(event.pos)
        )


# ============================================================
# Texture Editor
# ============================================================

class TextureEditor:

    def __init__(self):

        pygame.init()

        self.screen = pygame.display.set_mode(
            (WINDOW_WIDTH, WINDOW_HEIGHT),
            pygame.RESIZABLE
        )

        pygame.display.set_caption(
            "768x768 Texture Editor"
        )

        self.clock = pygame.time.Clock()

        self.font = pygame.font.SysFont(
            "consolas",
            16
        )

        self.small_font = pygame.font.SysFont(
            "consolas",
            13
        )

        self.large_font = pygame.font.SysFont(
            "consolas",
            19,
            bold=True
        )

        # ----------------------------------------------------
        # Canvas
        # ----------------------------------------------------

        self.canvas_x = (
            PATTERN_PANEL_WIDTH + 35
        )

        self.canvas_y = 90

        self.zoom = DEFAULT_ZOOM
        self.prerendered_canvas = None

        # ----------------------------------------------------
        # Pattern state
        # ----------------------------------------------------

        self.patterns = []

        self.selected_pattern = None
        self.selected_placement = None

        self.pattern_scroll = 0
        self.placement_scroll = 0

        # ----------------------------------------------------
        # Dragging
        # ----------------------------------------------------

        self.dragging = False

        self.drag_offset_x = 0
        self.drag_offset_y = 0

        # ----------------------------------------------------
        # Display
        # ----------------------------------------------------

        self.show_grid = True

        self.status = "Ready"

        self.make_buttons()

        self.load_patterns()

    # ========================================================
    # Buttons
    # ========================================================

    def make_buttons(self):

        y = 48

        self.buttons = {

            "add":
                Button(
                    pygame.Rect(
                        320, y, 120, 30
                    ),
                    "Add Placement"
                ),

            "duplicate":
                Button(
                    pygame.Rect(
                        445, y, 105, 30
                    ),
                    "Duplicate"
                ),

            "delete":
                Button(
                    pygame.Rect(
                        555, y, 85, 30
                    ),
                    "Delete"
                ),

            "up":
                Button(
                    pygame.Rect(
                        645, y, 80, 30
                    ),
                    "Layer Up"
                ),

            "down":
                Button(
                    pygame.Rect(
                        730, y, 90, 30
                    ),
                    "Layer Down"
                ),

            "hmirror":
                Button(
                    pygame.Rect(
                        825, y, 90, 30
                    ),
                    "H Mirror"
                ),

            "vmirror":
                Button(
                    pygame.Rect(
                        920, y, 90, 30
                    ),
                    "V Mirror"
                ),

            "dmirror":
                Button(
                    pygame.Rect(
                        1015, y, 90, 30
                    ),
                    "D Mirror"
                ),

            "red":
                Button(
                    pygame.Rect(
                        1110, y, 90, 30
                    ),
                    "Red"
                ),

            "reload":
                Button(
                    pygame.Rect(
                        1205, y, 115, 30
                    ),
                    "Reload"
                ),
        }

    # ========================================================
    # Pattern Loading
    # ========================================================

    def load_patterns(self):

        old_data = {}

        # Preserve existing placement data while reloading
        # the actual image files.
        for pattern in self.patterns:
            old_data[
                os.path.basename(pattern.filename).lower()
            ] = [
                placement.to_dict()
                for placement in pattern.placements
            ]

        self.patterns.clear()

        os.makedirs(
            PATTERN_DIR,
            exist_ok=True
        )

        filenames = []

        for filename in os.listdir(
            PATTERN_DIR
        ):

            path = os.path.join(
                PATTERN_DIR,
                filename
            )

            if not os.path.isfile(path):
                continue

            if filename.lower().endswith(
                SUPPORTED_EXTENSIONS
            ):
                filenames.append(filename)

        filenames.sort(
            key=lambda s: s.lower()
        )

        filenames = filenames[:MAX_PATTERNS]

        for index, filename in enumerate(
            filenames
        ):

            path = os.path.join(
                PATTERN_DIR,
                filename
            )

            try:

                pattern = Pattern(
                    index,
                    path
                )

                previous = old_data.get(
                    filename.lower()
                )

                if previous is not None:

                    pattern.placements = [
                        Placement.from_dict(item)
                        for item in previous
                    ]

                self.patterns.append(
                    pattern
                )

            except Exception as e:

                print(
                    f"Could not load {path}: {e}"
                )

        if not self.patterns:

            self.selected_pattern = None
            self.selected_placement = None

            self.status = (
                "No patterns found"
            )

            return

        if (
            self.selected_pattern is None
            or self.selected_pattern >= len(self.patterns)
        ):

            self.selected_pattern = 0

        self.selected_placement = None

        self.status = (
            f"Loaded {len(self.patterns)} patterns"
        )

    # ========================================================
    # Selected Pattern
    # ========================================================

    def current_pattern(self):

        if self.selected_pattern is None:
            return None

        if not (
            0 <= self.selected_pattern
            < len(self.patterns)
        ):
            return None

        return self.patterns[
            self.selected_pattern
        ]

    # ========================================================
    # Canvas
    # ========================================================

    def canvas_rect(self):

        return pygame.Rect(
            self.canvas_x,
            self.canvas_y,
            CANVAS_SIZE * self.zoom,
            CANVAS_SIZE * self.zoom
        )

    # --------------------------------------------------------

    def inside_canvas(self, pos):

        return self.canvas_rect().collidepoint(
            pos
        )

    # --------------------------------------------------------

    def screen_to_canvas(self, x, y):

        return (
            int(
                (x - self.canvas_x)
                / self.zoom
            ),
            int(
                (y - self.canvas_y)
                / self.zoom
            )
        )

    # ========================================================
    # Mirrored Instances
    # ========================================================

    def get_instances(
        self,
        pattern,
        placement
    ):
        """
        Turn one logical placement into 1, 2, or 4
        actual rendered instances.

        A horizontal mirror reflects across the
        vertical boundaries of the 128x128 texture.

        A vertical mirror reflects across the
        horizontal boundaries of the 128x128 texture.
        """

        width = pattern.width
        height = pattern.height

        instances = []

        red = placement.red
        # Original.
        instances.append(
            (
                placement.x,
                placement.y,
                False,
                False,
                red
            )
        )
        tile_x = placement.x//128
        tile_y = placement.y//128
        placement_in_tile_x = placement.x%128
        placement_in_tile_y = placement.y%128
        tile_start_x = tile_x*128
        tile_end_x = tile_start_x+127
        tile_start_y = tile_y*128
        tile_end_y = tile_start_y+127

        # Horizontal reflected copy.
        if placement.hmirror:
            x = (tile_end_x - placement_in_tile_x - width)

            instances.append(
                (
                    x,
                    placement.y,
                    True,
                    False,
                    red
                )
            )

        # Vertical reflected copy.
        if placement.vmirror:

            y = (tile_end_y - placement_in_tile_y - height)

            instances.append(
                (
                    placement.x,
                    y,
                    False,
                    True,
                    red
                )
            )

        # Diagonal reflection.
        if (
            (placement.hmirror and placement.vmirror) or (placement.dmirror)
        ):

            x = (tile_end_x - placement_in_tile_x - width)
            y = (tile_end_y - placement_in_tile_y - height)

            instances.append(
                (
                    x,
                    y,
                    True,
                    True,
                    red
                )
            )


        return instances

    # ========================================================
    # Composite
    # ========================================================

    def composite_texture(self):

        result = pygame.Surface(
            (CANVAS_SIZE, CANVAS_SIZE),
            pygame.SRCALPHA
        )

        result.fill(
            (0, 0, 0, 0)
        )

        # Pattern order determines which pattern
        # is later in the texture.
        for pattern in self.patterns:

            # Placement order within a pattern
            # also determines overwrite order.
            for placement in pattern.placements:

                instances = self.get_instances(
                    pattern,
                    placement
                )

                for (
                    x,
                    y,
                    hflip,
                    vflip,
                    red
                ) in instances:

                    image = pygame.transform.flip(
                        pattern.image,
                        hflip,
                        vflip
                    )
                    width = image.get_width()
                    height = image.get_height()

                    for sy in range(height):

                        dst_y = y + sy

                        if not (
                            0 <= dst_y < CANVAS_SIZE
                        ):
                            continue

                        for sx in range(width):

                            dst_x = x + sx

                            if not (
                                0 <= dst_x < CANVAS_SIZE
                            ):
                                continue

                            color = image.get_at(
                                (sx, sy)
                            )

                            # White = no pixel.
                            if is_white(color):
                                continue

                            if red:
                                color = (179, 0, 24)

                            # Non-white pixels overwrite
                            # whatever was already there.
                            result.set_at(
                                (dst_x, dst_y),
                                color
                            )

        return result

    # ========================================================
    # Drawing
    # ========================================================

    def draw(self):

        self.screen.fill(BG)
        self.draw_canvas()
        self.draw_info_panel()

        self.draw_pattern_panel()

        self.draw_toolbar()

        self.draw_status()

        pygame.display.flip()

    # ========================================================
    # Pattern Panel
    # ========================================================

    def draw_pattern_panel(self):

        height = self.screen.get_height()

        pygame.draw.rect(
            self.screen,
            PANEL,
            (
                0,
                0,
                PATTERN_PANEL_WIDTH,
                height
            )
        )

        self.draw_text(
            "PATTERNS",
            18,
            14,
            ACCENT,
            self.large_font
        )

        self.draw_text(
            f"{len(self.patterns)} / {MAX_PATTERNS}",
            210,
            18,
            TEXT_DIM,
            self.small_font
        )

        self.draw_text(
            "./patterns",
            18,
            40,
            TEXT_DIM,
            self.small_font
        )

        list_top = 65
        item_height = 105

        list_height = (
            height - list_top - 10
        )

        total_height = (
            len(self.patterns)
            * item_height
        )

        max_scroll = max(
            0,
            total_height - list_height
        )

        self.pattern_scroll = max(
            0,
            min(
                self.pattern_scroll,
                max_scroll
            )
        )

        clip = pygame.Rect(
            0,
            list_top,
            PATTERN_PANEL_WIDTH,
            list_height
        )

        old_clip = self.screen.get_clip()

        self.screen.set_clip(clip)

        for index, pattern in enumerate(
            self.patterns
        ):

            y = (
                list_top
                + index * item_height
                - self.pattern_scroll
            )

            rect = pygame.Rect(
                10,
                y,
                PATTERN_PANEL_WIDTH - 20,
                94
            )

            selected = (
                index == self.selected_pattern
            )

            pygame.draw.rect(
                self.screen,
                (
                    ACCENT_DARK
                    if selected
                    else PANEL_DARK
                ),
                rect,
                border_radius=4
            )

            pygame.draw.rect(
                self.screen,
                (
                    ACCENT
                    if selected
                    else BORDER
                ),
                rect,
                2 if selected else 1,
                border_radius=4
            )

            self.screen.blit(
                pattern.thumbnail,
                (
                    rect.x + 6,
                    rect.y + 8
                )
            )

            self.draw_text(
                f"{index:02d}",
                rect.x + 114,
                rect.y + 8,
                GREEN if selected else TEXT,
                self.font
            )

            name = pattern.name

            if len(name) > 21:
                name = name[:18] + "..."

            self.draw_text(
                name,
                rect.x + 114,
                rect.y + 32,
                TEXT,
                self.small_font
            )

            self.draw_text(
                (
                    f"{pattern.width} x "
                    f"{pattern.height}"
                ),
                rect.x + 114,
                rect.y + 52,
                TEXT_DIM,
                self.small_font
            )

            self.draw_text(
                f"{len(pattern.placements)} placements",
                rect.x + 114,
                rect.y + 69,
                TEXT_DIM,
                self.small_font
            )

        self.screen.set_clip(old_clip)

    # ========================================================
    # Toolbar
    # ========================================================

    def draw_toolbar(self):

        pygame.draw.rect(
            self.screen,
            PANEL,
            (
                PATTERN_PANEL_WIDTH,
                0,
                self.screen.get_width()
                - PATTERN_PANEL_WIDTH,
                85
            )
        )

        pattern = self.current_pattern()

        if pattern:

            self.draw_text(
                f"PATTERN {pattern.index:02d}: {pattern.name}",
                PATTERN_PANEL_WIDTH, #self.canvas_x,
                13,
                TEXT,
                self.large_font
            )

        else:

            self.draw_text(
                "768 x 768 TEXTURE",
                self.canvas_x,
                13,
                TEXT,
                self.large_font
            )

        mouse_pos = pygame.mouse.get_pos()

        for button in self.buttons.values():

            button.draw(
                self.screen,
                self.small_font,
                mouse_pos
            )

    # ========================================================
    # Canvas
    # ========================================================


    def draw_canvas(self):

        rect = self.canvas_rect()

        # Checkerboard.
        
        if self.prerendered_canvas is None:
            self.prerendered_canvas = pygame.Surface((CANVAS_SIZE, CANVAS_SIZE))

            for y in range(CANVAS_SIZE):

                for x in range(CANVAS_SIZE):

                    color = (
                        CHECKER_A
                        if (
                            (x // 4 + y // 4) & 1
                        ) == 0
                        else CHECKER_B
                    )

                    pygame.draw.rect(

                        self.prerendered_canvas,
                        color,
                        (x, y, 1, 1)
                    )
        scaled_canvas = pygame.transform.scale(self.prerendered_canvas, (CANVAS_SIZE*self.zoom, CANVAS_SIZE*self.zoom))
        self.screen.blit(scaled_canvas, (self.canvas_x, self.canvas_y), None)
        
        # Composite texture.
        texture = self.composite_texture()

        scaled = pygame.transform.scale(
            texture,
            (
                CANVAS_SIZE * self.zoom,
                CANVAS_SIZE * self.zoom
            )
        )

        self.screen.blit(
            scaled,
            rect.topleft
        )

        if self.show_grid:

            for x in range(CANVAS_SIZE + 1):

                sx = (
                    self.canvas_x
                    + x * self.zoom
                )
                if x % 128 == 0:
                    grid_col = TILE_GRID
                elif self.zoom >= 3:
                    grid_col = GRID
                else:
                    continue


                pygame.draw.line(
                    self.screen,
                    grid_col,
                    (sx, self.canvas_y),
                    (
                        sx,
                        self.canvas_y
                        + CANVAS_SIZE * self.zoom
                    )
                )

            for y in range(CANVAS_SIZE + 1):

                sy = (
                    self.canvas_y
                    + y * self.zoom
                )
                if y % 128 == 0:
                    grid_col = TILE_GRID
                elif self.zoom >= 3:
                    grid_col = GRID
                else:
                    continue

                pygame.draw.line(
                    self.screen,
                    grid_col,
                    (self.canvas_x, sy),
                    (
                        self.canvas_x
                        + CANVAS_SIZE * self.zoom,
                        sy
                    )
                )

        # Highlight placements belonging to the
        # currently selected pattern.
        pattern = self.current_pattern()

        if pattern:

            for index, placement in enumerate(
                pattern.placements
            ):

                for (
                    x,
                    y,
                    hflip,
                    vflip,
                    red
                ) in self.get_instances(
                    pattern,
                    placement
                ):

                    r = pygame.Rect(
                        self.canvas_x
                        + x * self.zoom,

                        self.canvas_y
                        + y * self.zoom,

                        pattern.width
                        * self.zoom,

                        pattern.height
                        * self.zoom
                    )

                    selected = (
                        index
                        == self.selected_placement
                    )

                    color = (
                        GREEN
                        if selected
                        else ACCENT
                    )

                    pygame.draw.rect(
                        self.screen,
                        color,
                        r,
                        2
                    )

        pygame.draw.rect(
            self.screen,
            BORDER,
            rect,
            2
        )

    # ========================================================
    # Information Panel
    # ========================================================

    def draw_info_panel(self):

        x = (
            self.canvas_x
            + CANVAS_SIZE * self.zoom
            + 20
        )

        y = self.canvas_y

        pattern = self.current_pattern()

        self.draw_text(
            "PLACEMENTS",
            x,
            y,
            ACCENT,
            self.large_font
        )

        y += 30

        if pattern:

            self.draw_text(
                (
                    f"Pattern {pattern.index:02d} "
                    f"· {len(pattern.placements)} records"
                ),
                x,
                y,
                TEXT_DIM,
                self.small_font
            )

        y += 25

        # ----------------------------------------------------
        # Placement list
        # ----------------------------------------------------

        list_height = 250

        pygame.draw.rect(
            self.screen,
            PANEL_DARK,
            (
                x - 8,
                y,
                INFO_PANEL_WIDTH - 20,
                list_height
            )
        )

        if pattern:

            row_height = 26

            for index, placement in enumerate(
                pattern.placements
            ):

                row_y = (
                    y
                    + 6
                    + index * row_height
                    - self.placement_scroll
                )

                if (
                    row_y < y
                    or row_y > y + list_height - 25
                ):
                    continue

                selected = (
                    index
                    == self.selected_placement
                )

                if selected:

                    pygame.draw.rect(
                        self.screen,
                        ACCENT_DARK,
                        (
                            x - 4,
                            row_y - 2,
                            INFO_PANEL_WIDTH - 30,
                            23
                        )
                    )

                mirrors = ""

                if placement.hmirror:
                    mirrors += " H"

                if placement.vmirror:
                    mirrors += " V"

                self.draw_text(
                    (
                        f"{index:02d}  "
                        f"X{placement.x:03d} "
                        f"Y{placement.y:03d}"
                        f"{mirrors}"
                    ),
                    x,
                    row_y,
                    TEXT if selected else TEXT_DIM,
                    self.small_font
                )

        y += list_height + 18

        # ----------------------------------------------------
        # Selected record
        # ----------------------------------------------------

        self.draw_text(
            "SELECTED",
            x,
            y,
            ACCENT,
            self.large_font
        )

        y += 30

        if (
            pattern is None
            or self.selected_placement is None
            or self.selected_placement
            >= len(pattern.placements)
        ):

            self.draw_text(
                "None",
                x,
                y,
                TEXT_DIM
            )

        else:

            placement = pattern.placements[
                self.selected_placement
            ]

            self.draw_text(
                f"X        : {placement.x:3d}",
                x,
                y
            )

            y += 21

            self.draw_text(
                f"Y        : {placement.y:3d}",
                x,
                y
            )

            y += 21

            self.draw_text(
                (
                    "H Mirror : "
                    + (
                        "ON"
                        if placement.hmirror
                        else "OFF"
                    )
                ),
                x,
                y,
                (
                    GREEN
                    if placement.hmirror
                    else TEXT
                )
            )

            y += 21

            self.draw_text(
                (
                    "V Mirror : "
                    + (
                        "ON"
                        if placement.vmirror
                        else "OFF"
                    )
                ),
                x,
                y,
                (
                    GREEN
                    if placement.vmirror
                    else TEXT
                )
            )

            y += 23

            copies = 1

            if placement.hmirror:
                copies *= 2

            if placement.vmirror:
                copies *= 2

            self.draw_text(
                f"Rendered : {copies} copies",
                x,
                y,
                TEXT_DIM
            )

            y += 22

            self.draw_text(
                f"24-bit   : ${placement.pack():04X}",
                x,
                y,
                GREEN
            )

        # ----------------------------------------------------
        # Controls
        # ----------------------------------------------------

        y += 42

        self.draw_text(
            "CONTROLS",
            x,
            y,
            ACCENT,
            self.large_font
        )

        y += 28

        controls = [
            "Drag          Move",
            "Arrow keys    Move 1 pixel",
            "Shift+Arrows  Move 8 pixels",
            "H             Toggle H mirror",
            "V             Toggle V mirror",
            "D             Toggle D mirror",
            "R             Toggle Red",
            "Delete        Delete placement",
            "Tab           Next placement",
            "G             Toggle grid",
            "+ / -         Zoom",
            "Ctrl+S        Save",
            "Ctrl+O        Load",
            "Ctrl+E        Export PNG",
            "Ctrl+B        Export binary",
        ]

        for line in controls:

            self.draw_text(
                line,
                x,
                y,
                TEXT_DIM,
                self.small_font
            )

            y += 18

    # ========================================================
    # Status
    # ========================================================

    def draw_status(self):

        height = self.screen.get_height()

        pygame.draw.rect(
            self.screen,
            PANEL_DARK,
            (
                0,
                height - 27,
                self.screen.get_width(),
                27
            )
        )

        self.draw_text(
            self.status,
            10,
            height - 21,
            TEXT_DIM,
            self.small_font
        )

    # ========================================================
    # Text
    # ========================================================

    def draw_text(
        self,
        text,
        x,
        y,
        color=TEXT,
        font=None
    ):

        if font is None:
            font = self.font

        surface = font.render(
            str(text),
            True,
            color
        )

        self.screen.blit(
            surface,
            (x, y)
        )

    # ========================================================
    # Pattern Selection
    # ========================================================

    def pattern_at(self, pos):

        x, y = pos

        if not (
            0 <= x < PATTERN_PANEL_WIDTH
        ):
            return None

        list_top = 65
        item_height = 105

        index = int(
            (
                y
                - list_top
                + self.pattern_scroll
            )
            / item_height
        )

        if (
            0 <= index
            < len(self.patterns)
        ):
            return index

        return None

    # ========================================================
    # Placement Selection
    # ========================================================

    def placement_at(self, pos):

        pattern = self.current_pattern()

        if pattern is None:
            return None

        # Search from the topmost/latest placement.
        for index in range(
            len(pattern.placements) - 1,
            -1,
            -1
        ):

            placement = pattern.placements[
                index
            ]

            for (
                x,
                y,
                hflip,
                vflip,
                red
            ) in self.get_instances(
                pattern,
                placement
            ):

                rect = pygame.Rect(
                    self.canvas_x
                    + x * self.zoom,

                    self.canvas_y
                    + y * self.zoom,

                    pattern.width
                    * self.zoom,

                    pattern.height
                    * self.zoom
                )

                if rect.collidepoint(pos):
                    return index

        return None

    # ========================================================
    # Placement List Selection
    # ========================================================

    def placement_list_at(self, pos):

        pattern = self.current_pattern()

        if pattern is None:
            return None

        x, y = pos

        info_x = (
            self.canvas_x
            + CANVAS_SIZE * self.zoom
            + 20
        )

        list_top = self.canvas_y + 55
        list_height = 250

        if not (
            info_x - 8
            <= x
            <= info_x + INFO_PANEL_WIDTH
        ):
            return None

        if not (
            list_top
            <= y
            <= list_top + list_height
        ):
            return None

        row_height = 26

        index = int(
            (
                y
                - list_top
                - 6
                + self.placement_scroll
            )
            / row_height
        )

        if (
            0 <= index
            < len(pattern.placements)
        ):
            return index

        return None

    # ========================================================
    # Add Placement
    # ========================================================

    def add_placement(self):

        pattern = self.current_pattern()

        if pattern is None:

            self.status = (
                "Select a pattern first"
            )

            return

        placement = Placement(
            x=0,
            y=0
        )

        pattern.placements.append(
            placement
        )

        self.selected_placement = (
            len(pattern.placements) - 1
        )

        self.status = (
            f"Added placement to "
            f"pattern {pattern.index:02d}"
        )

    # ========================================================
    # Duplicate
    # ========================================================

    def duplicate_selected(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        source = pattern.placements[
            self.selected_placement
        ]

        duplicate = Placement(
            x=min(CANVAS_SIZE-1, source.x + 4),
            y=min(CANVAS_SIZE-1, source.y + 4),
            hmirror=source.hmirror,
            vmirror=source.vmirror,
            dmirror=source.dmirror
        )

        index = (
            self.selected_placement + 1
        )

        pattern.placements.insert(
            index,
            duplicate
        )

        self.selected_placement = index

        self.status = "Duplicated placement"

    # ========================================================
    # Delete
    # ========================================================

    def delete_selected(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        index = self.selected_placement

        del pattern.placements[index]

        if not pattern.placements:

            self.selected_placement = None

        else:

            self.selected_placement = min(
                index,
                len(pattern.placements) - 1
            )

        self.status = "Deleted placement"

    # ========================================================
    # Layer Up
    # ========================================================

    def layer_up(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        index = self.selected_placement

        if index >= len(pattern.placements) - 1:
            return

        pattern.placements[index], pattern.placements[index + 1] = (
            pattern.placements[index + 1],
            pattern.placements[index]
        )

        self.selected_placement = index + 1

    # ========================================================
    # Layer Down
    # ========================================================

    def layer_down(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        index = self.selected_placement

        if index <= 0:
            return

        pattern.placements[index], pattern.placements[index - 1] = (
            pattern.placements[index - 1],
            pattern.placements[index]
        )

        self.selected_placement = index - 1

    # ========================================================
    # Mirrors
    # ========================================================

    def toggle_hmirror(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        placement = pattern.placements[
            self.selected_placement
        ]

        placement.hmirror = (
            not placement.hmirror
        )

    # --------------------------------------------------------

    def toggle_vmirror(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        placement = pattern.placements[
            self.selected_placement
        ]

        placement.vmirror = (
            not placement.vmirror
        )

    def toggle_dmirror(self):

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        placement = pattern.placements[
            self.selected_placement
        ]

        if placement.dmirror == False:
            placement.vmirror = False
            placement.hmirror = False

        placement.dmirror = (not placement.dmirror)


    def toggle_red(self):
        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        placement = pattern.placements[
            self.selected_placement
        ]

        placement.red = (not placement.red)

    # ========================================================
    # Mouse
    # ========================================================

    def mouse_down(self, event):

        # ----------------------------------------------------
        # Toolbar
        # ----------------------------------------------------

        for name, button in self.buttons.items():

            if not button.clicked(event):
                continue

            if name == "add":
                self.add_placement()

            elif name == "duplicate":
                self.duplicate_selected()

            elif name == "delete":
                self.delete_selected()

            elif name == "up":
                self.layer_up()

            elif name == "down":
                self.layer_down()

            elif name == "hmirror":
                self.toggle_hmirror()

            elif name == "vmirror":
                self.toggle_vmirror()

            elif name == "dmirror":
                self.toggle_dmirror()

            elif name == "red":
                self.toggle_red()

            elif name == "reload":
                self.load_patterns()

            return

        # ----------------------------------------------------
        # Pattern list
        # ----------------------------------------------------

        if event.pos[0] < PATTERN_PANEL_WIDTH:

            index = self.pattern_at(
                event.pos
            )

            if index is not None:

                self.selected_pattern = index
                self.selected_placement = None

                self.status = (
                    f"Selected pattern "
                    f"{index:02d}"
                )

            return

        # ----------------------------------------------------
        # Placement list
        # ----------------------------------------------------

        placement_index = (
            self.placement_list_at(
                event.pos
            )
        )

        if placement_index is not None:

            self.selected_placement = (
                placement_index
            )

            self.status = (
                f"Selected placement "
                f"{placement_index}"
            )

            return

        # ----------------------------------------------------
        # Canvas
        # ----------------------------------------------------

        if self.inside_canvas(
            event.pos
        ):

            selected = self.placement_at(
                event.pos
            )

            if selected is None:

                self.selected_placement = None
                return

            self.selected_placement = selected

            pattern = self.current_pattern()

            placement = pattern.placements[
                selected
            ]

            cx, cy = self.screen_to_canvas(
                *event.pos
            )

            self.drag_offset_x = (
                cx - placement.x
            )

            self.drag_offset_y = (
                cy - placement.y
            )

            self.dragging = True

    # ========================================================

    def mouse_up(self, event):

        if event.button == 1:
            self.dragging = False

    # ========================================================

    def mouse_motion(self, event):

        if not self.dragging:
            return

        pattern = self.current_pattern()

        if pattern is None:
            return

        if self.selected_placement is None:
            return

        placement = pattern.placements[
            self.selected_placement
        ]

        cx, cy = self.screen_to_canvas(
            *event.pos
        )

        placement.x = max(
            0,
            min(
                CANVAS_SIZE-1,
                cx - self.drag_offset_x
            )
        )

        placement.y = max(
            0,
            min(
                CANVAS_SIZE-1,
                cy - self.drag_offset_y
            )
        )

    # ========================================================
    # Mouse Wheel
    # ========================================================

    def mouse_wheel(self, event):

        mouse_x, mouse_y = pygame.mouse.get_pos()

        # Pattern panel.
        if mouse_x < PATTERN_PANEL_WIDTH:

            item_height = 105

            list_height = (
                self.screen.get_height()
                - 65
                - 10
            )

            max_scroll = max(
                0,
                len(self.patterns)
                * item_height
                - list_height
            )

            self.pattern_scroll -= (
                event.y * 50
            )

            self.pattern_scroll = max(
                0,
                min(
                    self.pattern_scroll,
                    max_scroll
                )
            )

            return

        # Placement list.
        pattern = self.current_pattern()

        if pattern:

            info_x = (
                self.canvas_x
                + CANVAS_SIZE * self.zoom
                + 20
            )

            if (
                info_x - 8
                <= mouse_x
                <= info_x + INFO_PANEL_WIDTH
            ):

                row_height = 26
                list_height = 250

                max_scroll = max(
                    0,
                    len(pattern.placements)
                    * row_height
                    - list_height
                )

                self.placement_scroll -= (
                    event.y * 40
                )

                self.placement_scroll = max(
                    0,
                    min(
                        self.placement_scroll,
                        max_scroll
                    )
                )

                return

        # Canvas zoom.
        if self.inside_canvas(
            (mouse_x, mouse_y)
        ):

            old_zoom = self.zoom

            new_zoom = max(
                MIN_ZOOM,
                min(
                    MAX_ZOOM,
                    old_zoom + event.y
                )
            )

            if new_zoom == old_zoom:
                return

            canvas_x = (
                mouse_x - self.canvas_x
            ) / old_zoom

            canvas_y = (
                mouse_y - self.canvas_y
            ) / old_zoom

            self.zoom = new_zoom

            self.canvas_x = int(
                mouse_x
                - canvas_x * self.zoom
            )

            self.canvas_y = int(
                mouse_y
                - canvas_y * self.zoom
            )

    # ========================================================
    # Keyboard
    # ========================================================

    def key_down(self, event):

        mods = pygame.key.get_mods()

        ctrl = bool(
            mods & pygame.KMOD_CTRL
        )

        shift = bool(
            mods & pygame.KMOD_SHIFT
        )

        # ----------------------------------------------------
        # File operations
        # ----------------------------------------------------

        if ctrl and event.key == pygame.K_s:

            self.save_project()
            return

        if ctrl and event.key == pygame.K_o:

            self.load_project()
            return

        if ctrl and event.key == pygame.K_e:

            self.export_png()
            return

        if ctrl and event.key == pygame.K_b:

            self.export_binary()
            return

        # ----------------------------------------------------
        # Grid
        # ----------------------------------------------------

        if event.key == pygame.K_g:

            self.show_grid = (
                not self.show_grid
            )

            return

        # ----------------------------------------------------
        # Zoom
        # ----------------------------------------------------

        if event.key in (
            pygame.K_EQUALS,
            pygame.K_PLUS
        ):

            self.zoom = min(
                MAX_ZOOM,
                self.zoom + 1
            )

            return

        if event.key == pygame.K_MINUS:

            self.zoom = max(
                MIN_ZOOM,
                self.zoom - 1
            )

            return

        # ----------------------------------------------------
        # Add
        # ----------------------------------------------------

        if event.key == pygame.K_INSERT:

            self.add_placement()
            return

        # ----------------------------------------------------
        # Delete
        # ----------------------------------------------------

        if event.key in (
            pygame.K_DELETE,
            pygame.K_BACKSPACE
        ):

            self.delete_selected()
            return

        # ----------------------------------------------------
        # Next placement
        # ----------------------------------------------------

        if event.key == pygame.K_TAB:

            pattern = self.current_pattern()

            if pattern is None:
                return

            if not pattern.placements:
                return

            if self.selected_placement is None:

                self.selected_placement = 0

            else:

                self.selected_placement = (
                    self.selected_placement + 1
                ) % len(pattern.placements)

            return

        # ----------------------------------------------------
        # Selected placement movement
        # ----------------------------------------------------

        pattern = self.current_pattern()

        #if pattern is None:
        #    return

        if self.selected_placement is not None:

            placement = pattern.placements[
                self.selected_placement
            ]

            step = 8 if shift else 1

            if event.key == pygame.K_LEFT:

                placement.x = max(
                    0,
                    placement.x - step
                )

            elif event.key == pygame.K_RIGHT:

                placement.x = min(
                    CANVAS_SIZE-1,
                    placement.x + step
                )

            elif event.key == pygame.K_UP:

                placement.y = max(
                    0,
                    placement.y - step
                )

            elif event.key == pygame.K_DOWN:

                placement.y = min(
                    CANVAS_SIZE-1,
                    placement.y + step
                )

            elif event.key == pygame.K_h:

                self.toggle_hmirror()

            elif event.key == pygame.K_v:

                self.toggle_vmirror()

            elif event.key == pygame.K_r:
                self.toggle_red()

            elif event.key == pygame.K_d:
                self.toggle_dmirror()
        else:
            if event.key == pygame.K_UP:
                self.canvas_y += 8
            elif event.key == pygame.K_DOWN:
                self.canvas_y -= 8
            elif event.key == pygame.K_LEFT:
                self.canvas_x += 8
            elif event.key == pygame.K_RIGHT:
                self.canvas_x -= 8
                

    # ========================================================
    # Save Project
    # ========================================================

    def save_project(self):

        data = {
            "version": 3,

            "patterns": [
                {
                    "filename": pattern.name,

                    "placements": [
                        placement.to_dict()
                        for placement
                        in pattern.placements
                    ]
                }

                for pattern in self.patterns
            ]
        }

        try:

            with open(
                PROJECT_FILE,
                "w",
                encoding="utf-8"
            ) as f:

                json.dump(
                    data,
                    f,
                    indent=2
                )

            self.status = (
                f"Saved {PROJECT_FILE}"
            )

        except Exception as e:

            self.status = (
                f"Save failed: {e}"
            )

    # ========================================================
    # Load Project
    # ========================================================

    def load_project(self):

        if not os.path.exists(
            PROJECT_FILE
        ):

            self.status = (
                f"{PROJECT_FILE} not found"
            )

            return

        try:

            with open(
                PROJECT_FILE,
                "r",
                encoding="utf-8"
            ) as f:

                data = json.load(f)

            by_filename = {}

            for item in data.get(
                "patterns",
                []
            ):

                filename = item.get(
                    "filename",
                    ""
                )

                placements = [
                    Placement.from_dict(p)
                    for p in item.get(
                        "placements",
                        []
                    )
                ]

                by_filename[
                    filename.lower()
                ] = placements

            for pattern in self.patterns:

                placements = by_filename.get(
                    pattern.name.lower()
                )

                if placements is not None:

                    pattern.placements = placements

                else:

                    pattern.placements = []

            self.selected_placement = None

            self.status = (
                f"Loaded {PROJECT_FILE}"
            )

        except Exception as e:

            self.status = (
                f"Load failed: {e}"
            )

    # ========================================================
    # PNG Export
    # ========================================================

    def export_png(self):

        os.makedirs(
            EXPORT_DIR,
            exist_ok=True
        )

        filename = os.path.join(
            EXPORT_DIR,
            "texture.png"
        )

        texture = self.composite_texture()

        pygame.image.save(
            texture,
            filename
        )

        self.status = (
            f"Exported {filename}"
        )

    # ========================================================
    # Binary Export
    # ========================================================

    def export_binary(self):

        """
        Export the 16-bit placement records.

        The file is grouped by pattern.

        For each pattern:

            uint16 placement_count

            uint16 placement
            uint16 placement
            ...

        All values are little-endian.

        The pattern itself is identified by its
        position in the pattern list / source directory.
        """

        os.makedirs(
            EXPORT_DIR,
            exist_ok=True
        )

        filename = os.path.join(
            EXPORT_DIR,
            "texture_records.bin"
        )

        try:

            with open(
                filename,
                "wb"
            ) as f:

                # Header.
                #f.write(
                #    b"TEX1"
                #)

                # Number of patterns.
                #f.write(
                #    struct.pack(
                #        "<B",
                #        len(self.patterns)
                #    )
                #)

                # num different pattern types

                # pattern index
                # num patterns of this type

                # pattern
                # pattern
                # pattern
                # ...

                num_patterns = sum([len(pattern.placements) for pattern in self.patterns])

                f.write(
                    struct.pack(
                        "<B", num_patterns
                    )
                )

                for pattern_idx,pattern in enumerate(self.patterns):
                    if len(pattern.placements) == 0:
                        continue
                    print("exporting pattern? with {} placements".format(len(pattern.placements)))

                    f.write(
                        struct.pack(
                            "<B", pattern_idx
                        )
                    )
                    f.write(
                        struct.pack(
                            "<B", len(pattern.placements)
                        )
                    )

                    for placement in pattern.placements:
                        packed_val = placement.pack()

                        f.write(
                            struct.pack(
                                "BBB", packed_val&0xFF, (packed_val>>8)&0xFF, (packed_val>>16)&0xFF
                            )
                        )
            self.status = (
                f"Exported {filename}"
            )

        except Exception as e:

            self.status = (
                f"Binary export failed: {e}"
            )

    # ========================================================
    # Main Loop
    # ========================================================

    def run(self):

        while True:

            for event in pygame.event.get():

                if event.type == pygame.QUIT:

                    pygame.quit()
                    return

                if event.type == pygame.KEYDOWN:

                    self.key_down(event)

                elif (
                    event.type
                    == pygame.MOUSEBUTTONDOWN
                ):

                    if event.button == 1:

                        self.mouse_down(event)

                elif (
                    event.type
                    == pygame.MOUSEBUTTONUP
                ):

                    if event.button == 1:

                        self.mouse_up(event)

                elif (
                    event.type
                    == pygame.MOUSEMOTION
                ):

                    self.mouse_motion(event)

                elif (
                    event.type
                    == pygame.MOUSEWHEEL
                ):

                    self.mouse_wheel(event)

            start = time.perf_counter()
            self.draw()
            end = time.perf_counter()
            execution_time = end - start
            #print(f"Function took {execution_time:.6f} seconds to complete.")
            self.clock.tick(144)


# ============================================================
# Main
# ============================================================

if __name__ == "__main__":

    editor = TextureEditor()
    pygame.key.set_repeat(500, 50)
    editor.run()