# How to Add a New Component

## 1. `Headers/Objects/Components.hpp`

1. Add the component enum to `ComponentType`.

   * The name must begin with `CMP_`.
   * Use `SNAKE_CASE`.

   Example:

   ```cpp
   CMP_AUDIO_SOURCE
   ```

2. Create the component struct.

   * The struct name must begin with `Component`.

   Example:

   ```cpp
   struct ComponentAudioSource
   ```

3. Add the component owner ID:

   ```cpp
   ID ownerID = static_cast<ID>(-1);
   ```

4. Add all fields required by the component.

5. When the struct contains member functions, implement them in:

   ```text
   Headers/Objects/Components.cpp
   ```

---

## 2. `Headers/Objects/Level.hpp`

Add a new `ComponentStorage` using the component struct:

```cpp
ComponentStorage<ComponentAudioSource> audioSources;
```

Follow the existing component-storage naming conventions.

---

## 3. `Headers/Objects/ComponentRegistry.hpp`

Add the component to the component registry preprocessor definition.

Each row represents the following:

1. Component struct name
2. `ComponentType` enum name
3. `ComponentStorage` member name
4. Translation JSON key

Use an existing registry entry as a reference for the required syntax.

---

## 4. `Headers/Objects/LuaWrappers.hpp`

1. Add a Lua wrapper struct for the new component.

2. Add the corresponding `Has()` and `Get()` functions to `ScriptEntity`.

Use the wrappers for existing components as references.

---

## 5. `src/Editor/ImGuiDrawFunctions.cpp`

1. Open `DrawComponentEditor()`.

2. Add the editor interface for the new component.

3. Use an existing component interface as a reference for layout, controls, and behavior.

4. Add the required translation key-value pairs to:

   ```text
   EngineAssets/Local/en.json
   ```

---

## 6. `src/Objects/Level.cpp`

### `ID Level::CreateEntity(Entity& copy)`

Add a logic block that copies the new component when duplicating or creating an entity from an existing entity.

Use an existing component block as a reference.

### `Level::DestroyEntity(const ID entityID)`

Remove the new component when its owning entity is deleted.

Add the appropriate removal call alongside the existing component-removal logic.

---

## 7. `src/Map/LevelSerilezation.cpp`

### `LoadComponents()`

1. Clear the new component storage at the beginning of the function.

2. Add a logic block that loads the component data from JSON.

3. Use the loading logic for an existing component as a reference.

### `SaveComponents()`

1. Create a new JSON array for the component at the beginning of the function.

2. Add a logic block that serializes the component data into the array.

3. Add the resulting array to the level JSON data.

4. Use the saving logic for an existing component as a reference.

---

## Notes

* Follow the existing naming conventions consistently.
* Use existing components as implementation references.
* In most cases, an existing component implementation can be copied and adapted by changing:

  * Component names
  * Enum names
  * Storage names
  * Translation keys
  * Serialized fields
  * Editor fields
  * Lua wrapper fields
* Ensure every required field is handled by both `LoadComponents()` and `SaveComponents()`.
* Ensure the component is removed when its owning entity is destroyed.
* Ensure entity-copy logic includes the component.
