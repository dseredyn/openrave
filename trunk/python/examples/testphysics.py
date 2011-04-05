#!/usr/bin/env python
# -*- coding: utf-8 -*-
# Copyright (C) 2009-2011 Rosen Diankov (rosen.diankov@gmail.com)
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Shows how to enable physics.

.. examplepre-block:: testphysics
  :image-width: 400

Description
-----------

When simulations are turned on, an internal timer starts and the SimulationStep functions of all classes are called. Note that simulations extend beyond physics. That's why there's the distinction between simulation and physics and both are set separately. To start the internal simulation with a timestep of 0.01 seconds do

.. code-block:: python

  env = Environment()
  env.StartSimulation(timestep=0.01)

To stop it do

.. code-block:: python

  env.StopSimulation()

In a plugin, every state is accessible directly through memory. In scripting (Octave/Matlab), there's a thread safe loop that serializes information to the socket. KinBody/Robot information can be accessed from any thread as long as EvironmentBase::LockPhysics is called. In a SimulationStep call, this is not necessary as OpenRAVE locks physics before calling it.

To set the physics engine to ODE, use

.. code-block:: python

  env.SetPhysicsEngine(env.CreatePhysicsEngine('ode'))

octave:

orEnvSetOptions('physics ODE');

To set gravity:

.. code-block:: python

  env.GetPhysicsEngine().SetGravity([0,0,-9.81])

Make sure that whatever object you don't want moving (like floors) are declared static. Setting Properites through XML Files

It is possible to create and setup a physics engine in the <environment> tag in the XML file description. The ode physics engine uses a custom XML reader to define a special odeproperties tag that can be used to specify friction, gravity, etc. For example:

.. code-block:: xml

  <environment>
    <!-- ... other definitions ... -->
    <physicsengine type="ode">
      <odeproperties>
        <friction>0.5</friction>
        <gravity>0 0 -9.8</gravity>
        <selfcollision>1</selfcollision>
      </odeproperties>
    </physicsengine>
  </environment>

Take a look at the **share/openrave/data/testphysics.env.xml** for a working example.

.. examplepost-block:: testphysics
"""
from __future__ import with_statement # for python 2.5
__author__ = 'Rosen Diankov'

import time
from openravepy import __build_doc__
if not __build_doc__:
    from openravepy import *
    from numpy import* 

def main(env,options):
    "Main example code."
    env.Load(options.scene)
    if options._physics is None:
        # no physics engine set, so set one
        physics = RaveCreatePhysicsEngine(env,'ode')
        env.SetPhysicsEngine(physics)
    env.GetPhysicsEngine().SetGravity([0,0,-9.81])
    bodynames = ['data/lego2.kinbody.xml', 'data/lego4.kinbody.xml', 'data/mug1.kinbody.xml']
    numbodies = 0
    env.StopSimulation()
    env.StartSimulation(timestep=0.01)
    starttime = time.time()
    while True:
        if numbodies < 40:
            with env:
                body = env.ReadKinBodyXMLFile(bodynames[random.randint(len(bodynames))])
                body.SetName('body%d'%numbodies)
                numbodies += 1
                env.AddKinBody(body)
                T = eye(4)
                T[0:3,3] = array((-0.5,-0.5,2))+0.4*random.rand(3)
                body.SetTransform(T)
                #env.GetRobots()[0].GetLinks()[6].GetGeometries()[0].SetCollisionMesh(TriMesh(*ComputeBoxMesh([1,0.2,0.3])))
                        
        time.sleep(0.4)
        simtime = env.GetSimulationTime()*1e-6
        realtime = time.time()-starttime
        print 'sim time: %fs, real time: %fs, diff = %fs'%(simtime,realtime,simtime-realtime)

from optparse import OptionParser
from openravepy import OpenRAVEGlobalArguments, with_destroy

@with_destroy
def run(args=None):
    """Command-line execution of the example.

    :param args: arguments for script to parse, if not specified will use sys.argv
    """
    parser = OptionParser(description="test physics")
    OpenRAVEGlobalArguments.addOptions(parser)
    parser.add_option('--scene',action="store",type='string',dest='scene',default='data/hanoi.env.xml',
                      help='Scene file to load (default=%default)')
    (options, leftargs) = parser.parse_args(args=args)
    env = OpenRAVEGlobalArguments.parseAndCreate(options,defaultviewer=True)
    main(env,options)

if __name__=='__main__':
    run()