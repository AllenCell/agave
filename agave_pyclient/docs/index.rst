Welcome to agave_pyclient's documentation!
==========================================

Quick start
===========

.. code-block:: console

   pip install agave_pyclient

You must have AGAVE installed separately. 

:ref:`agave-pyer-label` will automatically try to connect to an already-running AGAVE server.
If it can't find one, it can attempt to launch a local server automatically. 
(Pass ``auto_launch=False`` to disable this, or
``agave_path="/path/to/agave"`` to point at a specific executable to launch.)

You can start AGAVE as a server yourself on the command line:

.. code-block:: console

   agave --server &


Import and use the :ref:`agave-pyer-label` class in your Python code.

.. code-block:: python

   from agave_pyclient import AgaveRenderer

   # 1. connect to the AGAVE server (launching one if needed)
   # see the documentation for options to control this behavior
   r = AgaveRenderer()
   # 2. tell it what data to load
   r.load_data("my_favorite.ome.tiff", 0, 0, 0, [], [])
   # 3. set some render settings (abbreviated list here)
   r.set_resolution(681, 612)
   r.background_color(0, 0, 0)
   r.render_iterations(128)
   r.set_primary_ray_step_size(4)
   r.set_secondary_ray_step_size(4)
   r.set_voxel_scale(0.270833, 0.270833, 0.53)
   r.exposure(0.75)
   r.density(28.7678)
   # 4. give the output a name
   r.session("output.png")
   # 5. wait for render and then save output
   r.redraw()
   # 6. disconnect when done
   r.close()  

If ``AgaveRenderer`` auto-launched a server for you, ``close()`` shuts that process down.  Otherwise,
``close()`` just disconnects from the server, leaving it running for other clients to use.
``AgaveRenderer`` can also be used as a context manager, which closes it
automatically:

.. code-block:: python

   from agave_pyclient import AgaveRenderer

   with AgaveRenderer() as r:
       r.load_data("my_favorite.ome.tiff")
       # ... set render settings as above...
       r.session("output.png")
       r.redraw()

.. toctree::
   :hidden:
   :maxdepth: 1
   :caption: Contents:

   Overview <self>
   agave_pyer
