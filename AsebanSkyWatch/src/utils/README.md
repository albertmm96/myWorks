# Geographic Utilities

Low-level geographic utilities implementing the **Web Mercator (EPSG:3857) / Slippy Map tile model** used by the application.

## Tile Projection

`tile_math` converts WGS84 longitude/latitude coordinates to discrete map tiles:

[
x=\left\lfloor\frac{\lambda+180}{360}2^z\right\rfloor
]

[
y=\left\lfloor
\frac{1}{2}\left(1-\frac{\ln(\tan\phi+\sec\phi)}{\pi}\right)2^z
\right\rfloor
]

where (z) is the zoom level, (\lambda) the longitude in degrees, and (\phi) the latitude in radians.

Latitude is limited to **±85.05112878°**, consistent with the finite Web Mercator tile extent. The inverse projection is used to recover geographic bounding boxes from tile indices.

### References

* **OGC Two Dimensional Tile Matrix Set** — `WebMercatorQuad`
* **EPSG Guidance / Method 1026** — Spherical Mercator projection
* **EPSG:3857** — WGS 84 / Pseudo-Mercator
* **OpenStreetMap Slippy Map Tilenames** — tile-index derivation
