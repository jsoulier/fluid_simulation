# Fluid Simulation

https://github.com/user-attachments/assets/853d471b-9394-48f2-8a3c-065d3707d43f

Implementation of [Fluid Simulation for Dummies](https://mikeash.com/pyblog/fluid-simulation-for-dummies.html) using the new SDL3 GPU API with compute shaders

### Building

#### Windows

Install the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) for glslc

```bash
git clone https://github.com/jsoulier/fluid_simulation --recurse-submodules
cd fluid_simulation
mkdir build
cd build
cmake ..
cmake --build . --parallel 8 --config Release
cd bin
./fluid_simulation.exe
```

#### Linux

```bash
git clone https://github.com/jsoulier/fluid_simulation --recurse-submodules
cd fluid_simulation
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel 8
cd bin
./fluid_simulation
```
