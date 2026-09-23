# Geometry, Shapes, and Coordinate Transformations across Modules

## Executive Summary

This project implements a multi-module geometry library in **Moss v0.1**, exploring the boundaries and mechanics of Moss's static polymorphism model across module boundaries.

The library defines core geometric entities (`Point`, `BoundingBox`, `Circle`, `Rectangle`, `Square`), structural traits (`Bounded`, `Measurable`, `Containment`, `Shape`), generic polymorphic coordinate transformations (`translate`, `scale`, `get_bbox`, `get_area`, `get_perimeter`, `shape_contains_point`, `bboxes_intersect`), and functional collection pipelines (`map`, `filter`, `sum`, `count`, `all`, `any`).

All 10 unit tests pass under native lowering (`margo test`), and end-to-end execution produces bit-for-bit identical output between native binaries (`margo run`) and the Fast Debug interpreter (`margo debug`).

During development, we discovered and characterized four critical limitations/defects in static specialization and module projection, documented below with minimal standalone reproducers in `reproducers/`.

---

## Architecture and Module Layout

The codebase is organized into four modular components and a test suite:

```
examples/swarm/polymorphism/geometry_modules/
├── Moss.toml
├── src/
│   ├── geo_types.moss     # Concrete shapes, Point, BoundingBox, structural traits & methods
│   ├── transforms.moss    # Polymorphic transformations operating across all shape types
│   ├── pipeline_ops.moss  # Generic collection pipelines over shape collections
│   └── main.moss          # End-to-end demonstration executable
├── tests/
│   └── test_geometry.moss # 10 unit tests exercising shapes, traits, collisions & pipelines
└── reproducers/
    ├── repro_01_field_access_not_generic.moss
    ├── repro_02_export_trait_parameter_invariant.moss
    ├── repro_03_vector_trait_rustc_leak.moss
    └── repro_04_untyped_container_indexing.moss
```

### Module Responsibilities

1. **`src/geo_types.moss`**
   - **Records**: `Point`, `BoundingBox`, `Circle`, `Rectangle`, `Square`.
   - **Structural Traits**:
     - `Bounded`: requires `fn bbox() -> BoundingBox`
     - `Measurable`: requires `fn area() -> Int` and `fn perimeter() -> Int`
     - `Containment`: requires `fn contains_point(p: Point) -> Bool`
     - `Shape`: compound structural trait requiring `bbox()`, `area()`, `perimeter()`, and `contains_point()`.
   - **Encapsulated Methods**: All concrete shape types implement `area()`, `perimeter()`, `bbox()`, `translate()`, `scale()`, and `contains_point()`.

2. **`src/transforms.moss`**
   - Implements reusable, unified coordinate transformations across module boundaries:
     - `get_area(s) -> Int`
     - `get_perimeter(s) -> Int`
     - `get_bbox(s) -> BoundingBox`
     - `translate(s, dx, dy)` (polymorphic across `Circle`, `Rectangle`, `Square`, `BoundingBox`, `Point`)
     - `scale(s, factor)` (polymorphic across `Circle`, `Rectangle`, `Square`, `BoundingBox`)
     - `shape_contains_point(s, p) -> Bool`
     - `bboxes_intersect(b1, b2) -> Bool`

3. **`src/pipeline_ops.moss`**
   - Generic functional collection pipelines operating over shape vectors:
     - `sum_areas(shapes)`: `shapes.map(_.area()) |> sum`
     - `sum_perimeters(shapes)`: `shapes.map(_.perimeter()) |> sum`
     - `filter_area_greater_than(shapes, min_area)`: `shapes.filter(_.area() > min_area)`
     - `all_fit_in_bbox(shapes, boundary)`: validates containment of all shape bounding boxes within a region.
     - `any_contains_point(shapes, p)`: tests if any shape contains a given point.

4. **`src/main.moss` & `tests/test_geometry.moss`**
   - Demonstrates and verifies all features end-to-end with unit tests (`test "..."` blocks with `assert` and `assertEqual`).

---

## Polymorphism Mechanisms Evaluated

### 1. What Compiled and Specialized Cleanly

- **Method-Based Static Specialization via Duck Typing**:
  Functions with untyped parameters that invoke member methods (e.g., `fn get_area(s) -> Int: return s.area()`) cleanly mark the AST node generic (`function.generic = true`). When called with `Circle`, `Rectangle`, or `Square`, the compiler specializes the function for each distinct concrete type at monomorphization time.
- **Cross-Module Monomorphization**:
  When generic transform functions in `transforms.moss` are called from `main.moss` or `test_geometry.moss`, the compiler successfully propagates and specializes the definitions across module boundaries.
- **Structural Trait Satisfaction**:
  `Circle`, `Rectangle`, and `Square` satisfy the `Shape` structural trait without any explicit `implements` declaration. Duck typing is verified purely by compile-time structural method matching.
- **Functional Pipelines with Method Projection**:
  Pipeline operations with method references and wildcards, e.g.:
  ```moss
  export fn sum_areas(shapes) -> Int:
    return shapes.map(_.area()) |> sum
  ```
  specialize correctly across concrete collections (`Vector[Circle]`, `Vector[Rectangle]`, `Vector[Square]`).
- **Unified Transform Signatures**:
  Instead of writing separate `translate_circle`, `translate_rect`, etc., callers use a single `translate(s, dx, dy)` or `scale(s, factor)` function that returns the specialized shape type.

---

## Where Reuse Broke Down: Defect Analysis & Reproducers

During development, we encountered four distinct breakdown points where natural abstract Moss code failed to compile or lower properly:

### 1. Untyped Field Access Fails to Trigger Generic Specialization
- **File**: `reproducers/repro_01_field_access_not_generic.moss`
- **Classification**: Specialization / inference defect
- **Symptom**:
  ```moss
  fn get_x(s) -> Int:
    return s.x

  c = Circle(x: 10, radius: 5)
  r = Rectangle(x: 20, width: 4)
  echo get_x(c)
  echo get_x(r) # ERROR: argument 1 to function 'get_x' has type 'Rectangle', expected 'Circle'
  ```
- **Root Cause**:
  In `src/moss.cpp:1561-1565`, member access `e.find('.')` checks constraints and records `ConstraintKind::Field`, but **fails to set `function.generic = true;`** (unlike method calls, which explicitly trigger genericity). As a result, the function monomorphizes immediately to the first concrete type passed, rejecting subsequent calls with other types having the identical field.
- **Workaround**:
  Encapsulate property access behind getter methods (e.g., `s.get_x()`), which correctly marks `function.generic = true`.

### 2. Exported Trait Parameter Invariant Crash in Multi-Module Code
- **File**: `reproducers/repro_02_export_trait_parameter_invariant.moss`
- **Classification**: Specialization/inference defect & module/interface projection defect
- **Symptom**:
  ```moss
  export trait Shape:
    fn area() -> Int

  export fn get_area(s: Shape) -> Int:
    return s.area()
  ```
  Fails during `moss check` with:
  ```
  error[MOSS_INTERNAL_OR_IO_ERROR]: internal synchronization invariant: unresolved concrete method effect target (at src/moss.cpp:4658)
  ```
- **Root Cause**:
  When a parameter is explicitly annotated with a trait type (`s: Shape`), `function.generic` remains `false`. During module interface export (`.mossi`), parameter leaf effect analysis (`LeafCaptureScope`, `src/moss.cpp:4378`) attempts to resolve concrete method effect targets on `Shape`. Because `Shape` is registered in `traits_` and not `objects_`, `resolve_method` fails, triggering `synchronization_require(!leaf_effect_capture_)`.
- **Workaround**:
  Leave the parameter untyped (`export fn get_area(s) -> Int: return s.area()`). Because `s` is untyped, `function.generic = true;`, which bypasses leaf effect capture and properly exports generic AST for downstream cross-module specialization.

### 3. Frontend Leak: `Vector[Trait]()` Accepted by Checker but Lowering to Invalid Rust
- **File**: `reproducers/repro_03_vector_trait_rustc_leak.moss`
- **Classification**: Frontend validation gap & native / Fast-Debug inconsistency
- **Symptom**:
  ```moss
  trait Shape:
    fn area() -> Int

  fn main():
    var shapes = Vector[Shape]()
    shapes.push(Circle(radius: 5))
  ```
  `moss check` succeeds with 0 diagnostics. Fast Debug (`moss run --interp`) executes without error. However, native build (`margo run` / `margo build`) fails with:
  ```
  error[E0425]: cannot find type `Shape` in this scope
  --> build/.../native/repro_03.rs
  | let mut shapes = std::vec::Vec::<Shape>::new();
  ```
- **Root Cause**:
  Moss v0.1 does not support dynamic trait objects (no dynamic dispatch or vtables). All traits are compile-time structural predicates. However, the frontend accepts `Vector[TraitName]` as a type without emitting a compile error. The native backend then attempts to emit Rust code using the trait name as a concrete Rust type, which rustc rejects.
- **Workaround**:
  Collections must be homogeneous concrete types (e.g. `Vector[Circle]`, `Vector[Rectangle]`). Heterogeneous shape processing must be handled via polymorphic functions or pipelines applied to concrete vectors. The Moss frontend should be enhanced to reject `Vector[<Trait>]` at compile time with a clear diagnostic (e.g. `TRAIT_COLLECTION_ELEMENT_UNSUPPORTED`).

### 4. Untyped Open Generic Container Indexing Rejection
- **File**: `reproducers/repro_04_untyped_container_indexing.moss`
- **Classification**: Specialization / type inference defect
- **Symptom**:
  ```moss
  fn sum_radii(shapes) -> Int:
    var total = 0
    var i = 0
    while i < shapes.count():
      s = shapes[i] # ERROR: value is not an indexable container
      total = total + s.radius
      i = i + 1
    return total
  ```
- **Root Cause**:
  The type-checker requires container indexing (`shapes[i]`) to have an established container type (`vector[` or `map[`). For untyped parameters (`shapes`), the container type constraint is not deferred until specialization, causing premature rejection.
- **Workaround**:
  Use functional pipeline operations (`shapes.map(...) |> sum`, `filter`, `reduce`) or annotate collection types when indexing is needed.

### 5. Cross-Module Direct Struct Field Access Lowered as Private Rust Fields
- **Classification**: Native lowering defect & module projection defect
- **Symptom**:
  Accessing `b.min_x` across module boundaries passes `moss check` and Fast Debug, but native lowering emits Rust struct definitions where fields lack `pub`:
  ```rust
  // src/moss.cpp:11698
  out << "  " << field.name << ": " << field.type << ",\n";
  ```
  Rustc rejects cross-module field access with `error[E0616]: field `min_x` of struct `BoundingBox` is private`.
- **Workaround**:
  Expose public getter methods on structs (e.g. `b.get_min_x()`, `b.width()`, `b.height()`).

### 6. Parenthesized Arithmetic Emits Rust Unused-Parens Lint Error (SWARM-031)
- **Classification**: Native backend code-generation defect
- **Symptom**:
  Writing `return 2 * (width + height)` lowers to `(2_i64).wrapping_mul(((self.width).wrapping_add(self.height)))`, which triggers rustc warning `#![deny(unused_parens)]`.
- **Workaround**:
  Decompose compound parenthesized arithmetic into intermediate local bindings:
  ```moss
  sum = width + height
  return 2 * sum
  ```

---

## Native vs Fast Debug Comparison

| Feature / Behavior | `moss check` | Fast Debug (`margo debug`) | Native Build (`margo run` / `margo test`) | Consistency Status |
| :--- | :---: | :---: | :---: | :--- |
| **Encapsulated method duck typing** | Pass | Pass | Pass | **100% Identical** |
| **Cross-module transform specialization** | Pass | Pass | Pass | **100% Identical** |
| **Pipeline transforms (`map`, `filter`, `sum`)** | Pass | Pass | Pass | **100% Identical** |
| **Unit tests (10 test cases)** | Pass | Pass (10/10) | Pass (10/10) | **100% Identical** |
| **Direct cross-module field access (`b.min_x`)** | Pass | Pass | Fail (`rustc E0616: field is private`) | **Divergence (Backend Bug)** |
| **`Vector[Shape]()` trait collections** | Pass | Pass | Fail (`rustc E0425: cannot find type`) | **Divergence (Frontend Leak)** |
| **Parenthesized math `2 * (w + h)`** | Pass | Pass | Fail (`rustc -D unused-parens`) | **Divergence (Backend Bug)** |

For all supported and properly encapsulated abstractions, **Fast Debug and Native lowering produce bit-for-bit identical results** across the entire 24-step stdout trace and all 10 unit test cases.

---

## Verification and Test Results

### 1. Static Checking
```bash
./moss check examples/swarm/polymorphism/geometry_modules/src/main.moss --json
```
Result: `{"ok": true, "diagnostics": []}`

### 2. Unit Testing
```bash
./margo test
```
Result:
```
PASS tests/test_geometry.moss:bounding box calculations
PASS tests/test_geometry.moss:bounding box intersection
PASS tests/test_geometry.moss:circle collection pipelines
PASS tests/test_geometry.moss:circle polymorphic transforms
PASS tests/test_geometry.moss:cross-shape collision via bounding box
PASS tests/test_geometry.moss:point creation and distance
PASS tests/test_geometry.moss:rectangle collection pipelines
PASS tests/test_geometry.moss:rectangle polymorphic transforms
PASS tests/test_geometry.moss:square collection pipelines
PASS tests/test_geometry.moss:square polymorphic transforms

10 passed
0 failed
```

### 3. Code Formatting
```bash
./moss fmt examples/swarm/polymorphism/geometry_modules --check
```
Result: Completely clean, 0 formatting discrepancies.

---

## Conclusions on Moss's Static Polymorphism Model

1. **Static duck typing works remarkably well when driven by member methods**:
   When methods are used as the structural interface, Moss's monomorphization engine cleanly specializes functions and pipelines across module boundaries without requiring any runtime overhead or trait dictionaries.
2. **Explicit trait annotations on exported functions are currently broken**:
   Because trait annotations are not recognized as generic type parameters during interface export, they trigger an internal leaf-effect invariant crash. Untyped parameters (`fn f(s)`) currently serve as the only reliable syntax for cross-module generic polymorphism.
3. **No runtime trait objects**:
   Developers transitioning from languages with dynamic polymorphism (Rust `dyn Trait`, Go interfaces, Java interfaces) must recognize that Moss collections cannot store heterogeneous trait values (`Vector[Trait]` is invalid). Collections must remain homogeneous, and heterogeneous aggregation should be handled using domain message boundaries, tagged unions, or tuple pairs.
4. **Visibility in native lowering requires attention**:
   Generated Rust structs should emit `pub` fields (`pub x: i64`) if Moss's module system allows cross-module field access, or the Moss frontend should enforce encapsulation by rejecting cross-module private field access during `moss check`.
