## Environment variables

[How to set up envioriment variables](https://gist.github.com/Thiago099/b45ec7832fb754325b29a61006bcd10c)

COMMONLIB_SSE_FOLDER

Clone this Repository, to somewhere safe and adds its path to this environment variable on Windows.

```bash
git clone --recursive https://github.com/alandtse/CommonLibVR
cd CommonLibVR
git checkout ng
```

If it does not compile, you could try cloning this repository. If I make any changes to commonlib, they will be here first.

https://github.com/Thiago099/CommonLibVR
  
## Optional ouput folder optional variables

- SKYRIM_FOLDER
- WILDLANDER_OWRT_FOLDER
- SKYRIM_OWRT_FOLDER
- SKYRIM_MODS_FOLDER2
- SKYRIM_MODS_FOLDER


## Description of the new features

https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/README.md

## Mesh backend

NIF resources are parsed with [ousnius/nifly](https://github.com/ousnius/nifly) and rendered by this plugin's own Direct3D 11 pipeline. The renderer no longer clones objects into Skyrim's `UI3DSceneManager` or asks the Creation Engine to draw menu meshes.

The mesh pipeline uses its own deferred context and off-screen color/depth resources on Skyrim's D3D11 device. Meshes start with three directional lights matching the original renderer defaults and may contain up to eight lights. Every light casts rasterized, alpha-tested shadows through a filtered shadow-map array. The renderer does not hook Skyrim's render loop or bind mesh state directly to Skyrim's immediate context. Initial creation waits for GPU completion, and save calls return after the output file has been written. A 10 ms task loop renders meshes whose existing `mustUpdate` or `alwaysUpdate` fields request an update.

The path-based and form-based APIs load loose or archived game resources. NIF paths may be passed either as full resource paths (`meshes\weapons\iron\Longsword.nif`) or as model paths returned by game forms (`weapons\iron\Longsword.nif`); the renderer normalizes the latter under `meshes\`. The legacy `NiAVObject` list overloads remain in the public header for ABI compatibility, but return `nullptr`: nifly consumes serialized NIF streams and cannot reconstruct one from arbitrary live scene objects.

The public API and its original create, save, and delete exports remain unchanged, so existing copied API headers continue to work.

NPC meshes created from a unique loaded actor automatically follow that actor's live FaceGen expression, modifier, and phoneme morphs. For non-unique actors, call `Mesh::SetFaceMorphSource(actor)` after creating the mesh to select the actor instance whose live face should be mirrored.

`Mesh::SetExpression(expression, value)` applies an NPC expression directly from the default TRI files of its head parts. This path does not require a loaded actor and works independently of Skyrim's paused FaceGen update. `Mesh::SetMorph(triPath, morphName, value)` and `Mesh::ClearFaceMorphs()` expose the lower-level direct morph controls.

Skyrim HKX clips animate the skeleton. Use direct TRI expressions for independent rendered faces, or `SetFaceMorphSource` when live expression, modifier, and phoneme mirroring is desired.

## Per-mesh lights and exposure

Each mesh owns its light handles. `Mesh::GetLightCount()` and `Mesh::GetLight(index)` inspect the current lights, `Mesh::ClearLights()` removes them, and `Mesh::AddLight(...)` adds a configured directional light. A returned `Internal::ILight*` remains valid until its parent mesh clears its lights or is deleted. Do not delete a light handle directly.

```cpp
MeshRenderingFrameworkAPI::Mesh mesh("meshes\\example.nif", 512, 512);
mesh.ClearLights();

MeshRenderingFrameworkAPI::Internal::ILight* light = mesh.AddLight(
    -0.4f, 0.6f, 0.7f,
    1.0f, 0.9f, 0.8f,
    1.5f);

if (light) {
    MeshRenderingFrameworkAPI::Internal::ILight_SetDirection(light, -0.2f, 0.8f, 0.5f);
    MeshRenderingFrameworkAPI::Internal::ILight_SetColor(light, 1.0f, 0.75f, 0.6f);
    MeshRenderingFrameworkAPI::Internal::ILight_SetStrength(light, 2.0f);
}

mesh.SetExposure(1.25f);
```

Directions must be finite and nonzero. Color components, strength, and exposure must be finite and nonnegative. Adding a ninth light fails and returns `nullptr`. Light and exposure changes automatically request a new render. Exposure is applied before tone mapping.
