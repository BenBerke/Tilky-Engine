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

## 5. `src/Runtime/Scripting/LuaComponentBindings.cpp`
1. Add a new lua.user_type for the component
2. Existing code should provide enough context to add a new component

## 6. `src/Runtime/Scripting/LuaEntityBindings.cpp`
1. Add a new property for the component
2. Existing code should provide enough context to add a new component

## 7. `src/Editor/ImGuiDrawFunctions.cpp`

1. Open `DrawComponentEditor()`.

2. Add the editor interface for the new component.

3. Use an existing component interface as a reference for layout, controls, and behavior.

4. Wrap each field's widgets in a `PrefabField prefabField("<json key>");` scope (the key it is saved under, see section 9), so prefab instances can mark and revert/apply that field.

5. Add the required translation key-value pairs to:

   ```text
   EngineAssets/Local/en.json
   ```

---

## 8. `src/Objects/Level.cpp`

### `ID Level::CreateEntity(Entity& copy)`

Add a logic block that copies the new component when duplicating or creating an entity from an existing entity.

Use an existing component block as a reference.

### `Level::DestroyEntity(const ID entityID)`

Remove the new component when its owning entity is deleted.

Add the appropriate removal call alongside the existing component-removal logic.

---

## 9. `src/Map/LevelSerialization.cpp`

### `ComponentFieldsToJson()` / `ComponentFieldsFromJson()`

1. Add an overload pair for the new component: its own fields to JSON and back, without `ownerID`/`instanceID`. They are declared for every component in `TILKY_COMPONENTS` by `LevelSerialization.hpp`.

2. Use an existing component's pair as a reference. `FromJson` returns `false` for data that can't be loaded.

### `LoadComponents()` / `SaveComponents()`

Add one `LoadComponentArray<T>(...)` and one `SaveComponentArray(...)` line with the component's JSON array key (e.g. `"sprites"`).

---

## 10. `src/Objects/Prefab.cpp`

Add a `TILKY_PREFAB_TRAITS(...)` line with the same JSON array key. Prefab files, instance syncing and overrides then handle the component.

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
* Ensure every required field is handled by both `ComponentFieldsToJson()` and `ComponentFieldsFromJson()`.
* Ensure the component is removed when its owning entity is destroyed.
* Ensure entity-copy logic includes the component.
