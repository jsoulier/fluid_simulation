# Fluid Simulation

https://github.com/user-attachments/assets/d82464e1-56ae-4e51-ac25-b4100dcfa2e9

https://github.com/user-attachments/assets/eaca28e0-9742-4566-9650-f7c273e18270

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
