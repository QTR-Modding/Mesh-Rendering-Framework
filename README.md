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

The path-based and form-based APIs load loose or archived game resources. NIF paths may be passed either as full resource paths (`meshes\weapons\iron\Longsword.nif`) or as model paths returned by game forms (`weapons\iron\Longsword.nif`); the renderer normalizes the latter under `meshes\`. The legacy `NiAVObject` list overloads remain in the public header for ABI compatibility, but return `nullptr`: nifly consumes serialized NIF streams and cannot reconstruct one from arbitrary live scene objects.

Existing plugins do not need to replace their copied `MeshRenderingFrameworkAPI` header. Resource-path normalization is performed inside the framework DLL, and the original exported create, save, and delete contracts remain available.
