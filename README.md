# Luna

Luna is the latest (and hopefully greatest) pet project of mine, in my efforts to understand how modern game engines work.
I've been an avid gamer for most of my life, and I've always been fascinated by how graphics and computing technology has come together to make such incredible experiences.

I'm also curious at heart, and I would love nothing more than to take those game engines and tear them apart to see how they tick.
But more than that, I want to understand these programs on a more fundamental level, and the best way to do that is to create my own.

This repository contains the combined efforts of the past few years of study, experimentation, and tribulations I've gone through on my journey.
The code contained within is not going to be optimal, but I leave it here in the hopes that someone like myself may see it and find some amount of inspiration, or even learn from it.

Luna is purely written as an educational experiment, and I have no intentions of ever releasing it commercially.

## Building

Luna has been built and tested using Clang and Windows. While I endeavor to keep things platform-agnostic and compliant, other compilers have not been tested and may produce errors.

### Prerequisites

- [CMake](https://cmake.org/) (Verion 3.21 or greater)
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (Version 1.3.xxx.x or greater)

### Build Steps

- `cmake -S . -B Build`
- `cmake --build Build --parallel 8`

### Running

- The final executable can be found in the `Build/Bin` folder.
- The editor expects to be run from the root of the repository. Otherwise it will fail to load assets.

## Credits

My journey has been long and hard, and it would not be possible without the inspiration and help of others in the game development community. Below, in no particular order, are just a few of the people and projects I would like to extend thanks to, or credit for code I've used along the way in this project.

* [Hans-Kristian Arntzen (Themaister)](https://github.com/Themaister) - For his work on the [Granite](https://github.com/Themaister/Granite) engine, and several helpful [blog posts](http://themaister.net/blog/). A lot of the way I use and abstract Vulkan has been inspired from him.
* [Travis Vroman (travisvroman)](https://github.com/travisvroman) - For his work on the [Kohi](https://github.com/travisvroman/kohi) engine. His return to the basics of C and hand-rolled libraries was inspiring and informative, and while I've chosen to stick with C++ for my project, I have learned a lot about low-level implementations from this series.
* [Flax Engine](https://github.com/FlaxEngine/FlaxEngine) - For providing a high-quality, open-source engine that I can look to as an example of how to architect the systems needed to make a game engine run.
* [Yan Chernikov (TheCherno)](https://github.com/TheCherno/) - For his work on the [Hazel](https://github.com/TheCherno/Hazel) engine (and the private development branch of Hazel). I was able to learn a lot about engine architecture and asset management from this example.
* [Matthew Albrecht (mattparks)](https://github.com/mattparks) - For his work on the [Acid](https://github.com/EQMG/Acid) engine. The clean, modern C++ infrastructure he developed was instrumental in helping me work out this engine's structure.
* [Panos Karabelas (PanosK92)](https://github.com/PanosK92) - For his work on the [Spartan](https://github.com/PanosK92/SpartanEngine) engine. The beautiful lighting and rendering techniques have driven me to strive for more, in the hopes I can one day match them.
* [The Khronos Group (KhronosGroup)](https://github.com/KhronosGroup) - For their work on the Vulkan API itself, as well as the [glTF](https://www.khronos.org/gltf/) model format, [KTX](https://www.khronos.org/ktx/) texture format, and [sample models](https://github.com/KhronosGroup/glTF-Sample-Models).
