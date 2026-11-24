"""OME-Zarr sources for the grid demo.

These match ``toloudis/zarr-webgpu-multicanvas-demo``'s ``ZARR_IMAGE_SOURCES``:
Allen Cell 20x timelapse wells, TCZYX uint16 with three channels and five
multiscale levels.
"""

_BASE = "https://s3.us-west-2.amazonaws.com/production.files.allencell.org"

ZARR_SOURCES = [
    f"{_BASE}/83b/79f/3a3/0ef/562/c89/347/c63/2b0/b0e/b3/3500007507_20250418_20X_Timelapse-01(P67-G2).ome.zarr",
    f"{_BASE}/466/78b/965/bfa/482/95a/a43/c79/013/0b4/45/3500007507_20250418_20X_Timelapse-01(P66-G2).ome.zarr",
    f"{_BASE}/8be/f68/830/0e7/fde/f1a/74c/48a/5c9/090/00/3500007507_20250418_20X_Timelapse-01(P65-G3).ome.zarr",
    f"{_BASE}/725/fbb/c38/46b/e92/268/648/440/df0/b94/d8/3500007507_20250418_20X_Timelapse-01(P64-G4).ome.zarr",
    f"{_BASE}/e83/e9e/6e9/280/c85/71b/f47/f04/697/89a/93/3500007507_20250418_20X_Timelapse-01(P63-G4).ome.zarr",
    f"{_BASE}/052/d56/601/424/6e9/b5b/b47/6d4/303/7f7/a0/3500007507_20250418_20X_Timelapse-01(P62-G4).ome.zarr",
    f"{_BASE}/9d4/18a/10d/cc7/52b/b8a/143/109/580/e89/2c/3500007507_20250418_20X_Timelapse-01(P61-G5).ome.zarr",
    f"{_BASE}/682/029/0cd/320/d34/668/842/6db/0d3/dc7/a6/3500007507_20250418_20X_Timelapse-01(P60-G5).ome.zarr",
    f"{_BASE}/cc1/783/842/b9b/44b/6a8/c4e/a03/3d0/120/f3/3500007507_20250418_20X_Timelapse-01(P6-C2).ome.zarr",
    f"{_BASE}/fdf/b7c/340/274/bc7/3cb/540/7c1/a49/2fe/59/3500007507_20250418_20X_Timelapse-01(P59-G5).ome.zarr",
    f"{_BASE}/330/220/782/bf8/6a5/cc8/f47/7f9/e4f/8fc/55/3500007507_20250418_20X_Timelapse-01(P58-G6).ome.zarr",
    f"{_BASE}/852/cd5/a93/c56/b5d/e48/f44/3e3/e23/f42/7f/3500007507_20250418_20X_Timelapse-01(P57-G6).ome.zarr",
    f"{_BASE}/9df/dc7/ff9/30e/9a4/4eb/f4e/9a6/1d0/485/dc/3500007507_20250418_20X_Timelapse-01(P56-G7).ome.zarr",
    f"{_BASE}/706/965/08f/a2f/b4c/80a/a43/169/c32/b8a/09/3500007507_20250418_20X_Timelapse-01(P55-G7).ome.zarr",
    f"{_BASE}/915/7e6/e19/8f4/030/9b9/240/48f/538/ba7/2d/3500007507_20250418_20X_Timelapse-01(P54-G7).ome.zarr",
    f"{_BASE}/3cd/a07/b82/7ce/9dc/afa/342/a0d/f01/ed6/3e/3500007507_20250418_20X_Timelapse-01(P53-G8).ome.zarr",
    f"{_BASE}/8cb/cfa/a8b/930/58d/599/944/a25/5bf/6df/63/3500007450_20250606_20x_Timelapse(P9-B5).ome.zarr",
    f"{_BASE}/aff/2f7/5fc/b69/174/faa/042/d61/a75/80d/32/3500007507_20250418_20X_Timelapse-01(P52-G8).ome.zarr",
    f"{_BASE}/9ce/045/b01/777/fff/0fa/64c/db8/1fc/9cd/2e/3500007450_20250606_20x_Timelapse(P8-B5).ome.zarr",
    f"{_BASE}/206/268/a9a/248/4e6/6c9/b4f/241/fe9/fcc/be/3500007507_20250418_20X_Timelapse-01(P51-G8).ome.zarr",
    f"{_BASE}/80e/a40/e38/f6d/91a/418/b47/888/b2e/7d4/d6/3500007450_20250606_20x_Timelapse(P72-E11).ome.zarr",
    f"{_BASE}/cf2/928/dbe/f5d/d08/b09/b4e/534/9a2/529/39/3500007450_20250606_20x_Timelapse(P71-E11).ome.zarr",
    f"{_BASE}/775/688/ade/d61/411/ae8/b4e/b92/744/ac4/3e/3500007507_20250418_20X_Timelapse-01(P50-G9).ome.zarr",
    f"{_BASE}/026/bf1/68f/7c9/64a/318/745/7a3/9df/c8e/9c/3500007450_20250606_20x_Timelapse(P70-E11).ome.zarr",
    f"{_BASE}/ee5/175/9f7/844/1b7/a08/74f/dc8/69c/1ef/f9/3500007507_20250418_20X_Timelapse-01(P5-C2).ome.zarr",
    f"{_BASE}/6a8/b9a/270/c1c/8c9/d49/141/277/8a6/c15/75/3500007507_20250418_20X_Timelapse-01(P49-G9).ome.zarr",
    f"{_BASE}/d20/74c/be0/d27/27b/8b9/343/1d5/f0a/c4e/11/3500007450_20250606_20x_Timelapse(P7-B5).ome.zarr",
    f"{_BASE}/0b0/f9f/d4c/757/081/ea8/34a/181/cec/bf1/eb/3500007507_20250418_20X_Timelapse-01(P48-G9).ome.zarr",
    f"{_BASE}/eb6/2de/4d1/ca0/958/7ba/a44/ca3/4af/2b7/77/3500007450_20250606_20x_Timelapse(P69-D10).ome.zarr",
    f"{_BASE}/d4a/6c4/a70/db8/9fe/cba/447/5ca/478/1bb/cd/3500007507_20250418_20X_Timelapse-01(P47-G10).ome.zarr",
    f"{_BASE}/0f8/d97/e7f/6cf/8e5/ebb/341/0a4/c59/e1c/02/3500007450_20250606_20x_Timelapse(P68-D10).ome.zarr",
    f"{_BASE}/7c1/8ea/dcb/63b/793/6d8/74a/edc/02f/31a/d4/3500007507_20250418_20X_Timelapse-01(P46-G10).ome.zarr",
    f"{_BASE}/b6c/296/ae0/7ec/ff2/b2b/24e/048/079/de7/7c/3500007450_20250606_20x_Timelapse(P67-D10).ome.zarr",
    f"{_BASE}/d93/74e/0aa/d5c/5d4/359/c43/6ea/9e5/58c/ee/3500007507_20250418_20X_Timelapse-01(P45-G10).ome.zarr",
    f"{_BASE}/3a9/fc6/f9d/c2b/cea/85b/e4d/8be/39e/d38/3f/3500007450_20250606_20x_Timelapse(P66-G8).ome.zarr",
    f"{_BASE}/b66/7cf/072/c56/298/3c8/d4c/d46/063/d4f/29/3500007507_20250418_20X_Timelapse-01(P44-G11).ome.zarr",
    f"{_BASE}/b12/2dc/79c/0f8/e2f/9e9/04e/5ba/12d/417/ae/3500007450_20250606_20x_Timelapse(P65-G8).ome.zarr",
    f"{_BASE}/22d/93b/015/707/9b0/049/c40/799/e12/5b7/8d/3500007507_20250418_20X_Timelapse-01(P43-G11).ome.zarr",
    f"{_BASE}/0b5/6a8/d8e/107/323/69a/b42/868/475/f58/4f/3500007450_20250606_20x_Timelapse(P64-G8).ome.zarr",
]
