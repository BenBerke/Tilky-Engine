# Localisation Guide
Do NOT touch the text to the left of “:” ONLY edit the right hand side

The grey text after two slashes (//) is an explanation to give information about the text
Please inform a contributor if you need further context

You can use any script you want (Arabic, Cyrillic, Latin etc.)

If your language is not supported by Noto Sans please inform a contributor. (But you can write to the translation file as norma)

To check if your language is supported by Noto Sans: https://fonts.google.com/noto/specimen/Noto+Sans

#### Do not forget to add commas after each line, except for the last line

#### Example Translation:

Original:
```json
"editor.title": "Editor",
```

Turkish Translation:
```json
"editor.title": "Düzenleyici", 
```

*Düzenleyici means Editor in Turkish*


# What to do in the Engine

Go to EngineAssets/Local, copy paste the en.json to the same directory, name it to your language (preferably a two letter language code) and edit it.

#### The JSON File with the context (do NOT copy paste this):
#### This is only to give context, make sure your .json file does not contain any comments

```json
"editor.title": "Editor",
"editor.mode": "Mode", // Mode as in “Creative mode”, “Survival Mode”
"editor.create_sector": "Create Sector", //“A sector is a room, a 2d polygon of any size and shape”
"editor.create_texture": "Create Texture", // A texture is an image
"editor.texture": "Texture",
"editor.background_texture": "Background Texture", // The skybox. The image that will be rendered if there are no walls or entities visible
"editor.floor": "Current Floor", // As in the floors in a building, NOT the floor you are walking on
"editor.save": "Save",
"editor.save_and_play": "Save & Play",
"editor.shutdown": "Shutdown", // Force quit without saving
"editor.level_name": "Level Name", // A level is a map


"levels.title": "Levels", // A level as in a video game level
"levels.load": "Load Level",
"levels.delete": "Delete Level",


"mode.dot": "Dot Mode", // A dot is an invisible “thing” placed on the map to help walls snap to the grid
"mode.wall": "Wall Mode",
"mode.sector": "Sector Mode",
"mode.entity": "Entity Mode", // You can imagine ann entity (can also be called “Object”) as a “blank box” that you put information in and it does stuff. For example, if you put a “image” in the box, it becomes visible on in the game. If you add “physics” in the box, it starts falling
"mode.unknown": "Unknown Mode", // If a bug occurs and no mode is found this text will appear


"sector.title": "Sector",
"sector.ceil_height": "Ceiling Height",
"sector.floor_height": "Floor Height", // The distance of the room’s floor from y = 0
"sector.floor_count": "Floor Count", // Floor as in the amount of stories in a building (i.e ground floor, first floor, second floor…)
"sector.ground_floor_texture": "Ground Floor Texture", // Texture of the area’s ground floor (an area can have multiple floors)
"sector.ceiling_texture": "Ceiling Texture",
"sector.ceiling_color": "Ceiling Color",
"sector.floor_color": "Floor Color", // As in the ground you are walking on


"wall.title": "Wall",
"wall.front_sector": "Front Sector", // The sector that is in front of the wall (In other words. if a sector is a room, than this wall is it’s south side)
"wall.back_sector": "Back Sector", // The sector that is below the wall (on a 2D top-down view. In other words: if the sector is a room, then this wall is it’s north side)
"wall.texture_index": "Texture Index",
"wall.floor": "Floor", // Which story of the building it is in.
"wall.color": "Wall Color",


"math.vector2.x" : "X", // Coordinate system
"math.vector2.y" : "Y",


"entity.name" : "Name",
"entity.title": "Entity",
"entity.add_component": "Add Component", // A component is basically a box of information (for example a Transform component would have x and y positions)
"entity.delete": "Remove",
"entity.components" : "Components",


"component.modify" : "Modify",


"component.component": "Component",
"component.transform": "Transform", // Stores information about the position, rotation etc. of the object
"component.sprite": "Sprite", // A sprite is a 2D image that can be placed anywhere in the world
"component.decal": "Decal", // A decal is a 2d image that is attached to a wall. Like a sticker
"component.player_spawn": "Player Spawn", // the position the player starts at


"component.transform.position" : "Position",
"component.transform.scale" : "Scale",
"component.transform.floor" : "Floor",


"component.sprite.texture_index" : "Texture Index",


"component.decal.attached_wall" : "Attached Wall", // The wall the decal is attached to
"component.decal.z_offset" : "Vertical Position",
"component.decal.wall_offset" : "Horizontal Position",
"component.decal.wall_normal_offset" : "Distance from Wall",
"component.decal.abs_height" : "Absolute Height", // This is a boolean, if this is true, the decal will not change vertical positions even if the wall it is attached to moves up or down


"common.delete": "Delete",
"common.close": "Close",
"common.add" : "Add",
"common.edit": "Edit",
"common.cancel" : "Cancel",
"common.id": "ID", // Identity (number)


"log.deleted_level": "Deleted level:",
"log.failed_delete_missing": "Failed to delete level, file may not exist:",
"log.delete_level_failed": "Delete level failed:",


"bug.unknown" : "Unknown",


"launcher.name": "Tilky Engine Launcher", // “Tilky” is a special name, do not change it (Unless you are using a different script or the rules of your language says it, then you can change it to a phonetic spelling in your system). Pronunciation at the end of the markdown
"launcher.projects": "Projects",
"launcher.create_project": "Create Project",
"launcher.create": "Create",
"launcher.input_name": "Project Name",
"launcher.open_project": "Open Project",
"launcher.delete_project": "Delete Project",
"launcher.quit": "Quit" // DO NOT ADD A COMMA HERE
}

```

# How to Pronounce Tilky
T as in **T**ea  
I as in **I**gloo  
L as in **L**ook  
K as in **K**eep  
Y as in **H**appy  

TIL.kee

International Phonetic Alphabet: [ˈtilki]

Arabic Spelling:
تِلْكِي 

