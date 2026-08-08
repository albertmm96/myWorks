# Geographic Utilities

Low-level geographic utilities implementing the **Web Mercator (EPSG:3857) / Slippy Map tile model** used by the application.

## Tile Projection

`tile_math` converts longitude/latitude coordinates into discrete map tiles.

For zoom level `z`:

$$
x = \left\lfloor \frac{\lambda + 180}{360} \cdot 2^z \right\rfloor
$$

$$
y = \left\lfloor
\frac{1}{2}
\left(
1 - \frac{\ln(\tan(\phi) + \sec(\phi))}{\pi}
\right)
\cdot 2^z
\right\rfloor
$$

where:

* `λ` is longitude in degrees
* `φ` is latitude in radians
* `z` is the map zoom level

Latitude is clamped to **±85.05112878°**, the practical Web Mercator limit. The inverse projection is used to recover geographic bounding boxes from tile indices.

## References

* **OGC Two Dimensional Tile Matrix Set** — `WebMercatorQuad`
* **EPSG:3857** — WGS 84 / Pseudo-Mercator
* **EPSG Method 1026** — Mercator (Spherical)
* **OpenStreetMap Slippy Map Tilenames** — tile-index derivation
