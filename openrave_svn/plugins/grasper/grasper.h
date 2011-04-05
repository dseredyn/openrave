// -*- coding: utf-8 -*-
// Copyright (C) 2006-2010 Carnegie Mellon University (rdiankov@cs.cmu.edu)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#ifndef  GRASPER_PROBLEM_H
#define  GRASPER_PROBLEM_H

#ifdef QHULL_FOUND

extern "C"
{
#include <qhull/qhull.h>
#include <qhull/mem.h>
#include <qhull/qset.h>
#include <qhull/geom.h>
#include <qhull/merge.h>
#include <qhull/poly.h>
#include <qhull/io.h>
#include <qhull/stat.h>
}

#endif

#define GTS_M_ICOSAHEDRON_X /* sqrt(sqrt(5)+1)/sqrt(2*sqrt(5)) */ \
  (dReal)0.850650808352039932181540497063011072240401406
#define GTS_M_ICOSAHEDRON_Y /* sqrt(2)/sqrt(5+sqrt(5))         */ \
  (dReal)0.525731112119133606025669084847876607285497935
#define GTS_M_ICOSAHEDRON_Z (dReal)0.0

template<class T1, class T2>
    struct sort_pair_first {
        bool operator()(const std::pair<T1,T2>&left, const std::pair<T1,T2>&right) {
            return left.first < right.first;
        }
    };

// very simple interface to use the GrasperPlanner
class GrasperProblem : public ProblemInstance
{
    struct GRASPANALYSIS
    {
    GRASPANALYSIS() : mindist(0), volume(0) {}
        dReal mindist;
        dReal volume;
    };

 public:
 GrasperProblem(EnvironmentBasePtr penv)  : ProblemInstance(penv), errfile(NULL) {
        __description = ":Interface Author: Rosen Diankov\n\nUsed to simulate a hand grasping an object by closing its fingers until collision with all links. ";
        RegisterCommand("Grasp",boost::bind(&GrasperProblem::Grasp,this,_1,_2),
                        "Performs a grasp and returns contact points");
        RegisterCommand("ComputeDistanceMap",boost::bind(&GrasperProblem::ComputeDistanceMap,this,_1,_2),
                        "Computes a distance map around a particular point in space");
        RegisterCommand("GetStableContacts",boost::bind(&GrasperProblem::GetStableContacts,this,_1,_2),
                        "Returns the stable contacts as defined by the closing direction");
        RegisterCommand("ConvexHull",boost::bind(&GrasperProblem::ConvexHull,this,_1,_2),
                        "Given a point cloud, returns information about its convex hull like normal planes, vertex indices, and triangle indices. Computed planes point outside the mesh, face indices are not ordered, triangles point outside the mesh (counter-clockwise)");
    }
    virtual ~GrasperProblem() {
        if( !!errfile )
            fclose(errfile);
    }
    
    virtual void Destroy()
    {
        _planner.reset();
        _robot.reset();
    }

    virtual int main(const std::string& args)
    {
        string strRobotName;
        stringstream ss(args);
        ss >> strRobotName;

        _report.reset(new CollisionReport());
        _robot = GetEnv()->GetRobot(strRobotName);

        string plannername = "Grasper";
        string cmd;
        while(!ss.eof()) {
            ss >> cmd;
            if( !ss ) {
                break;
            }
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "planner" ) {
                ss >> plannername;
            }
            if( ss.fail() || !ss ) {
                break;
            }
        }

        _planner = RaveCreatePlanner(GetEnv(),plannername);
        if( !_planner ) {
            RAVELOG_WARN("Failed to create planner\n");
            return -1;
        }

        return 0;
    }

    virtual bool SendCommand(std::ostream& sout, std::istream& sinput)
    {
        EnvironmentMutex::scoped_lock lock(GetEnv()->GetMutex());
        return ProblemInstance::SendCommand(sout,sinput);
    }

    virtual bool Grasp(std::ostream& sout, std::istream& sinput)
    {
        string strsavetraj;
        bool bGetLinkCollisions = false;
        bool bExecute = true;
        bool bComputeStableContacts = false;
        bool bComputeForceClosure = false;
        bool bOutputFinal = false;
        dReal friction = 0;

        boost::shared_ptr<GraspParameters> params(new GraspParameters(GetEnv()));
        params->btransformrobot = true;
        params->bonlycontacttarget = true;
        params->btightgrasp = false;
        params->vtargetdirection = Vector(0,0,1);
        boost::shared_ptr<CollisionCheckerMngr> pcheckermngr;

        string cmd;
        while(!sinput.eof()) {
            sinput >> cmd;
            if( !sinput )
                break;
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "body" || cmd == "target" ) {
                string name; sinput >> name;
                params->targetbody = GetEnv()->GetKinBody(name);
                if( !params->targetbody )
                    RAVELOG_WARN(str(boost::format("failed to find target %s\n")%name));
            }
            else if( cmd == "bodyid" ) {
                int id = 0; sinput >> id;
                params->targetbody = GetEnv()->GetBodyFromEnvironmentId(id);
            }
            else if( cmd == "direction" ) {
                sinput >> params->vtargetdirection.x >> params->vtargetdirection.y >> params->vtargetdirection.z;
                params->vtargetdirection.normalize3();
            }
            else if( cmd == "avoidlink" ) {
                string linkname;
                sinput >> linkname;
                params->vavoidlinkgeometry.push_back(linkname);
            }
            else if( cmd == "notrans" )
                params->btransformrobot = false;
            else if( cmd == "transformrobot" )
                sinput >> params->btransformrobot;
            else if( cmd == "onlycontacttarget" )
                sinput >> params->bonlycontacttarget;
            else if( cmd == "tightgrasp" )
                sinput >> params->btightgrasp;
            else if( cmd == "execute" )
                sinput >> bExecute;
            else if( cmd == "writetraj" )
                sinput >> strsavetraj;
            else if( cmd == "outputfinal" )
                sinput >> bOutputFinal;
            else if( cmd == "graspingnoise" )
                sinput >> params->fgraspingnoise;
            else if( cmd == "roll" )
                sinput >> params->ftargetroll;
            else if( cmd == "centeroffset" || cmd == "position" )
                sinput >> params->vtargetposition.x >> params->vtargetposition.y >> params->vtargetposition.z;
            else if( cmd == "standoff" )
                sinput >> params->fstandoff;
            else if( cmd == "friction" )
                sinput >> friction;
            else if( cmd == "getlinkcollisions" )
                bGetLinkCollisions = true;
            else if( cmd == "stablecontacts" )
                sinput >> bComputeStableContacts;
            else if( cmd == "forceclosure" )
                sinput >> bComputeForceClosure;
            else if( cmd == "collision" ) {
                string name; sinput >> name;
                pcheckermngr.reset(new CollisionCheckerMngr(GetEnv(), name));
            }
            else if( cmd == "translationstepmult" )
                sinput >> params->ftranslationstepmult;
            else {
                RAVELOG_WARN(str(boost::format("unrecognized command: %s\n")%cmd));
                break;
            }

            if( !sinput ) {
                RAVELOG_ERROR(str(boost::format("failed processing command %s\n")%cmd));
                return false;
            }
        }

        boost::shared_ptr<KinBody::KinBodyStateSaver> bodysaver;
        if( !!params->targetbody )
            bodysaver.reset(new KinBody::KinBodyStateSaver(params->targetbody));

        RobotBase::RobotStateSaver saver(_robot);
        _robot->Enable(true);

        params->SetRobotActiveJoints(_robot);
        _robot->GetActiveDOFValues(params->vinitialconfig);
    
        if( !_planner->InitPlan(_robot, params) ) {
            RAVELOG_WARN("InitPlan failed\n");
            return false;
        }

        TrajectoryBasePtr ptraj = RaveCreateTrajectory(GetEnv(),_robot->GetActiveDOF());
        if( !_planner->PlanPath(ptraj) || ptraj->GetPoints().size() == 0 )
            return false;

        ptraj->CalcTrajTiming(_robot, ptraj->GetInterpMethod(), true, true);
        TrajectoryBasePtr pfulltraj = RaveCreateTrajectory(GetEnv(),_robot->GetDOF());
        _robot->GetFullTrajectoryFromActive(pfulltraj,ptraj,false);

        if( strsavetraj.size() > 0 ) {
            ofstream f(strsavetraj.c_str());
            pfulltraj->Write(f, 0);
        }

        bodysaver.reset(); // restore target
        BOOST_ASSERT(ptraj->GetPoints().size()>0);
        _robot->SetTransform(ptraj->GetPoints().back().trans);
        _robot->SetActiveDOFValues(ptraj->GetPoints().back().q);

        vector< pair<CollisionReport::CONTACT,int> > contacts;
        if( bComputeStableContacts ) {
            Vector vworlddirection = !params->targetbody ? params->vtargetdirection : params->targetbody->GetTransform().rotate(params->vtargetdirection);
            _GetStableContacts(contacts, vworlddirection, friction);
        }
        else {
            // calculate the contact normals
            GetEnv()->GetCollisionChecker()->SetCollisionOptions(CO_Contacts);
            std::vector<KinBody::LinkPtr> vlinks;
            _robot->GetActiveManipulator()->GetChildLinks(vlinks);
            FOREACHC(itlink, vlinks) {
                if( GetEnv()->CheckCollision(KinBody::LinkConstPtr(*itlink), KinBodyConstPtr(params->targetbody), _report) ) {
                    RAVELOG_VERBOSE(str(boost::format("contact %s\n")%_report->__str__()));
                    FOREACH(itcontact,_report->contacts) {
                        if( _report->plink1 != *itlink ) {
                            itcontact->norm = -itcontact->norm;
                            itcontact->depth = -itcontact->depth;
                        }
                        contacts.push_back(make_pair(*itcontact,(*itlink)->GetIndex()));
                    }
                }
            }
            GetEnv()->GetCollisionChecker()->SetCollisionOptions(0);
        }

        RAVELOG_VERBOSE(str(boost::format("number of contacts: %d\n")%contacts.size()));
        FOREACH(itcontact,contacts) {
            Vector norm = itcontact->first.norm;
            Vector pos = itcontact->first.pos;//-norm*itcontact->first.depth; //?
            sout << pos.x <<" " << pos.y <<" " << pos.z <<" " << norm.x <<" " << norm.y <<" " << norm.z <<" ";
            if(bGetLinkCollisions)
                sout << itcontact->second << " ";
            sout << endl;
        }

        if( bOutputFinal ) {
            BOOST_ASSERT(pfulltraj->GetPoints().size()>0);
            sout << pfulltraj->GetPoints().back().trans << " ";
            FOREACHC(it,pfulltraj->GetPoints().back().q)
                sout << *it << " ";
        }

        GRASPANALYSIS analysis;
        if( bComputeForceClosure ) {
            try {
                vector<CollisionReport::CONTACT> c(contacts.size());
                for(size_t i = 0; i < c.size(); ++i)
                    c[i] = contacts[i].first;
                analysis = _AnalyzeContacts3D(c,friction,8);
            }
            catch(const openrave_exception& ex) {
                RAVELOG_WARN("AnalyzeContacts3D: %s\n",ex.what());
            }
            sout << analysis.mindist << " " << analysis.volume << " ";
        }

        if( bExecute )
            _robot->SetMotion(pfulltraj);
        
        return true;
    }

    virtual bool ComputeDistanceMap(std::ostream& sout, std::istream& sinput)
    {
        dReal conewidth = 0.25f*PI;
        int nDistMapSamples = 60000;
        string cmd;
        KinBodyPtr targetbody;
        Vector vmapcenter;
        while(!sinput.eof()) {
            sinput >> cmd;
            if( !sinput )
                break;
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "conewidth" )
                sinput >> conewidth;
            else if( cmd == "mapsamples" )
                sinput >> nDistMapSamples;
            else if( cmd == "target" ) {
                string name; sinput >> name;
                targetbody = GetEnv()->GetKinBody(name);
            }
            else if( cmd == "center" )
                sinput >> vmapcenter.x >> vmapcenter.y >> vmapcenter.z;
            else {
                RAVELOG_WARN(str(boost::format("unrecognized command: %s\n")%cmd));
                break;
            }

            if( !sinput ) {
                RAVELOG_ERROR(str(boost::format("failed processing command %s\n")%cmd));
                return false;
            }
        }

        RobotBase::RobotStateSaver saver1(_robot);
        KinBody::KinBodyStateSaver saver2(targetbody);
        _robot->Enable(false);
        targetbody->Enable(true);

        vector<CollisionReport::CONTACT> vpoints;        
        BoxSample(targetbody,vpoints,nDistMapSamples,vmapcenter);
        //DeterministicallySample(targetbody, vpoints, 4, vmapcenter);

        targetbody->Enable(false);
        _ComputeDistanceMap(vpoints, conewidth);
        FOREACH(itpoint, vpoints) {
            sout << itpoint->depth << " " << itpoint->norm.x << " " << itpoint->norm.y << " " << itpoint->norm.z << " ";
            sout << itpoint->pos.x - vmapcenter.x << " " << itpoint->pos.y - vmapcenter.y << " " << itpoint->pos.z - vmapcenter.z << "\n";
        }

        return true;
    }

    virtual bool GetStableContacts(std::ostream& sout, std::istream& sinput)
    {
        string cmd;
        dReal mu=0;
        Vector direction;
        bool bGetLinkCollisions = false;
        while(!sinput.eof()) {
            sinput >> cmd;
            if( !sinput ) {
                break;
            }
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "direction" ) {
                sinput >> direction.x >> direction.y >> direction.z;
            }
            else if( cmd == "friction" ) {
                sinput >> mu;
            }
            else if( cmd == "getlinkcollisions" ) {
                bGetLinkCollisions = true;
            }
            else {
                RAVELOG_WARN(str(boost::format("unrecognized command: %s\n")%cmd));
                break;
            }

            if( !sinput ) {
                RAVELOG_ERROR(str(boost::format("failed processing command %s\n")%cmd));
                return false;
            }
        }

        vector< pair<CollisionReport::CONTACT,int> > contacts;
        _GetStableContacts(contacts, direction, mu);
        FOREACH(itcontact,contacts) {
            Vector pos = itcontact->first.pos, norm = itcontact->first.norm;
            sout << pos.x <<" " << pos.y <<" " << pos.z <<" " << norm.x <<" " << norm.y <<" " << norm.z <<" ";
            if(bGetLinkCollisions)
                sout << itcontact->second << " ";
            sout << endl;
        }

        return true;
    }

    virtual bool ConvexHull(std::ostream& sout, std::istream& sinput)
    {
        string cmd;
        bool bReturnFaces = true, bReturnPlanes = true, bReturnTriangles = true;
        int dim=0;
        vector<double> vpoints;
        while(!sinput.eof()) {
            sinput >> cmd;
            if( !sinput ) {
                break;
            }
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if( cmd == "points" ) {
                int N=0;
                sinput >> N >> dim;
                vpoints.resize(N*dim);
                for(int i = 0; i < N*dim; ++i) {
                    sinput >> vpoints[i];
                }
            }
            else if( cmd == "returnplanes" ) {
                sinput >> bReturnPlanes;
            }
            else if( cmd == "returnfaces" ) {
                sinput >> bReturnFaces;
            }
            else if( cmd == "returntriangles" ) {
                sinput >> bReturnFaces;
            }
            else {
                RAVELOG_WARN(str(boost::format("unrecognized command: %s\n")%cmd));
                break;
            }

            if( !sinput ) {
                RAVELOG_ERROR(str(boost::format("failed processing command %s\n")%cmd));
                return false;
            }
        }

        vector<double> vconvexplanes;
        boost::shared_ptr< vector<int> > vconvexfaces;
        if( bReturnFaces || bReturnTriangles ) {
            vconvexfaces.reset(new vector<int>);
        }
        if( !_ComputeConvexHull(vpoints,vconvexplanes, vconvexfaces, dim) ) {
            return false;
        }
        if( bReturnPlanes ) {
            sout << vconvexplanes.size()/(dim+1) << " ";
            FOREACH(it,vconvexplanes) {
                sout << *it << " ";
            }
        }
        if( bReturnFaces ) {
            FOREACH(it,*vconvexfaces) {
                sout << *it << " ";
            }
        }
        if( bReturnTriangles ) {
            if( dim != 3 ) {
                RAVELOG_WARN(str(boost::format("cannot triangulate convex hulls of dimension %d\n")%dim));
                return false;
            }
            size_t faceindex = 1;
            int numtriangles = 0;
            while(faceindex < vconvexfaces->size()) {
                numtriangles += vconvexfaces->at(faceindex)-2;
                faceindex += vconvexfaces->at(faceindex)+1;
            }
            sout << numtriangles << " ";
            faceindex = 1;
            size_t planeindex = 0;
            vector<double> meanpoint(dim,0), point0(dim,0), point1(dim,0);
            vector<pair<double,int> > angles;
            while(faceindex < vconvexfaces->size()) {
                // have to first sort the vertices of the face before triangulating them
                // point* = point-mean
                // atan2(plane^T * (point0* x point1*), point0*^T * point1*) = angle <- sort
                int numpoints = vconvexfaces->at(faceindex);
                for(int j = 0; j < dim; ++j) {
                    meanpoint[j] = 0;
                    point0[j] = 0;
                    point1[j] = 0;
                }
                for(int i = 0; i < numpoints; ++i) {
                    int pointindex = vconvexfaces->at(faceindex+i+1);
                    for(int j = 0; j < dim; ++j) {
                        meanpoint[j] += vpoints[pointindex*dim+j];
                    }
                }
                int pointindex0 = vconvexfaces->at(faceindex+1);
                for(int j = 0; j < dim; ++j) {
                    meanpoint[j] /= numpoints;
                    point0[j] = vpoints[pointindex0*dim+j] - meanpoint[j];
                }
                angles.resize(numpoints); angles[0].first = 0; angles[0].second = 0;
                for(int i = 1; i < numpoints; ++i) {
                    int pointindex = vconvexfaces->at(faceindex+i+1);
                    for(int j = 0; j < dim; ++j) {
                        point1[j] = vpoints[pointindex*dim+j] - meanpoint[j];
                    }
                    dReal sinang = vconvexplanes[planeindex+0] * (point0[1]*point1[2] - point0[2]*point1[1]) + vconvexplanes[planeindex+1] * (point0[2]*point1[0] - point0[0]*point1[2]) + vconvexplanes[planeindex+2] * (point0[0]*point1[1] - point0[1]*point1[0]);
                    dReal cosang = point0[0]*point1[0] + point0[1]*point1[1] + point0[2]*point1[2];
                    angles[i].first = RaveAtan2(sinang,cosang);
                    if( angles[i].first < 0 ) {
                        angles[i].first += 2*PI;
                    }
                    angles[i].second = i;
                }
                sort(angles.begin(),angles.end(),sort_pair_first<double,int>());
                for(size_t i = 2; i < angles.size(); ++i) {
                    sout << vconvexfaces->at(faceindex+1+angles[0].second) << " " << vconvexfaces->at(faceindex+1+angles[i-1].second) << " " << vconvexfaces->at(faceindex+1+angles[i].second) << " ";
                }
                faceindex += numpoints+1;
                planeindex += dim+1;
            }
        }
        return true;
    }

 protected:
    void SampleObject(KinBodyPtr pbody, vector<CollisionReport::CONTACT>& vpoints, int N, Vector graspcenter)
    {
        RAY r;
        Vector com = graspcenter;
        GetEnv()->GetCollisionChecker()->SetCollisionOptions(CO_Contacts|CO_Distance);
        
        vpoints.resize(N);
        int i = 0;

        while(i < N) {
            r.dir.z = 2*RaveRandomFloat()-1;
            dReal R = RaveSqrt(1 - r.dir.x * r.dir.x);
            dReal U2 = 2 * PI * RaveRandomFloat();
            r.dir.x = R * RaveCos(U2);
            r.dir.y = R * RaveSin(U2);

            r.pos = com - 10.0f*r.dir;
            r.dir *= 1000;

            if( GetEnv()->CheckCollision(r, KinBodyConstPtr(pbody), _report) ) {
                vpoints[i].norm = _report->contacts.at(0).norm;
                vpoints[i].pos = _report->contacts.at(0).pos + 0.001f * vpoints[i].norm; // extrude a little
                vpoints[i].depth = 0;
                i++;
            }
        }

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(0);
    }

    // generates samples across a geodesic sphere (the higher the level, the higher the number of points
    void DeterministicallySample(KinBodyPtr pbody, vector<CollisionReport::CONTACT>& vpoints, int levels, Vector graspcenter)
    {
        RAY r;
        KinBody::Link::TRIMESH tri;
        Vector com = graspcenter;
        GenerateSphereTriangulation(tri,levels);

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(CO_Contacts|CO_Distance);

        // take the mean across every tri
        vpoints.reserve(tri.indices.size()/3);
        for(int i = 0; i < (int)tri.indices.size(); i += 3) {
            r.dir = 0.33333f * (tri.vertices[tri.indices[i]] + tri.vertices[tri.indices[i+1]] + tri.vertices[tri.indices[i+2]]);
            r.dir.normalize3();
            r.dir *= 1000;
        
            r.pos = com - 10.0f*r.dir;
            CollisionReport::CONTACT p;
            if( GetEnv()->CheckCollision(r, KinBodyConstPtr(pbody), _report) ) {
                p.norm = -_report->contacts.at(0).norm;//-r.dir//_report->contacts.at(0).norm1;
                p.pos = _report->contacts.at(0).pos + 0.001f * p.norm; // extrude a little
                p.depth = 0;
                vpoints.push_back(p);
            }
        }

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(0);
    }

    // generate a sphere triangulation starting with an icosahedron
    // all triangles are oriented counter clockwise
    void GenerateSphereTriangulation(KinBody::Link::TRIMESH& tri, int levels)
    {
        KinBody::Link::TRIMESH temp, temp2;

        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_Z, +GTS_M_ICOSAHEDRON_X, -GTS_M_ICOSAHEDRON_Y));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_X, +GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z, -GTS_M_ICOSAHEDRON_X));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z, +GTS_M_ICOSAHEDRON_X));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_X, -GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_Z, +GTS_M_ICOSAHEDRON_X, +GTS_M_ICOSAHEDRON_Y));
        temp.vertices.push_back(Vector(-GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z, +GTS_M_ICOSAHEDRON_X));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_Z, -GTS_M_ICOSAHEDRON_X, -GTS_M_ICOSAHEDRON_Y));
        temp.vertices.push_back(Vector(-GTS_M_ICOSAHEDRON_X, +GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z));
        temp.vertices.push_back(Vector(-GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z, -GTS_M_ICOSAHEDRON_X));
        temp.vertices.push_back(Vector(-GTS_M_ICOSAHEDRON_X, -GTS_M_ICOSAHEDRON_Y, +GTS_M_ICOSAHEDRON_Z));
        temp.vertices.push_back(Vector(+GTS_M_ICOSAHEDRON_Z, -GTS_M_ICOSAHEDRON_X, +GTS_M_ICOSAHEDRON_Y));

        const int nindices=60;
        int indices[nindices] = {
            0, 1, 2,
            1, 3, 4,
            3, 5, 6,
            2, 4, 7,
            5, 6, 8,
            2, 7, 9,
            0, 5, 8,
            7, 9, 10,
            0, 1, 5,
            7, 10, 11,
            1, 3, 5,
            6, 10, 11,
            3, 6, 11,
            9, 10, 8,
            3, 4, 11,
            6, 8, 10,
            4, 7, 11,
            1, 2, 4,
            0, 8, 9,
            0, 2, 9
        };

        Vector v[3];
    
        // make sure oriented CCW 
        for(int i = 0; i < nindices; i += 3 ) {
            v[0] = temp.vertices[indices[i]];
            v[1] = temp.vertices[indices[i+1]];
            v[2] = temp.vertices[indices[i+2]];
            if( v[0].dot3((v[1]-v[0]).cross(v[2]-v[0])) < 0 )
                swap(indices[i], indices[i+1]);
        }

        temp.indices.resize(nindices);
        std::copy(&indices[0],&indices[nindices],temp.indices.begin());

        KinBody::Link::TRIMESH* pcur = &temp;
        KinBody::Link::TRIMESH* pnew = &temp2;
        while(levels-- > 0) {

            pnew->vertices.resize(0);
            pnew->vertices.reserve(2*pcur->vertices.size());
            pnew->vertices.insert(pnew->vertices.end(), pcur->vertices.begin(), pcur->vertices.end());
            pnew->indices.resize(0);
            pnew->indices.reserve(4*pcur->indices.size());

            map< uint64_t, int > mapnewinds;
            map< uint64_t, int >::iterator it;

            for(size_t i = 0; i < pcur->indices.size(); i += 3) {
                // for ever tri, create 3 new vertices and 4 new triangles.
                v[0] = pcur->vertices[pcur->indices[i]];
                v[1] = pcur->vertices[pcur->indices[i+1]];
                v[2] = pcur->vertices[pcur->indices[i+2]];

                int inds[3];
                for(int j = 0; j < 3; ++j) {
                    uint64_t key = ((uint64_t)pcur->indices[i+j]<<32)|(uint64_t)pcur->indices[i + ((j+1)%3) ];
                    it = mapnewinds.find(key);

                    if( it == mapnewinds.end() ) {
                        inds[j] = mapnewinds[key] = mapnewinds[(key<<32)|(key>>32)] = (int)pnew->vertices.size();
                        pnew->vertices.push_back((v[j]+v[(j+1)%3 ]).normalize3());
                    }
                    else {
                        inds[j] = it->second;
                    }
                }

                pnew->indices.push_back(pcur->indices[i]);    pnew->indices.push_back(inds[0]);    pnew->indices.push_back(inds[2]);
                pnew->indices.push_back(inds[0]);    pnew->indices.push_back(pcur->indices[i+1]);    pnew->indices.push_back(inds[1]);
                pnew->indices.push_back(inds[2]);    pnew->indices.push_back(inds[0]);    pnew->indices.push_back(inds[1]);
                pnew->indices.push_back(inds[2]);    pnew->indices.push_back(inds[1]);    pnew->indices.push_back(pcur->indices[i+2]);
            }

            swap(pnew,pcur);
        }

        tri = *pcur;
    }

    void BoxSample(KinBodyPtr pbody, vector<CollisionReport::CONTACT>& vpoints, int num_samples, Vector center)
    {
        RAY r;
        KinBody::Link::TRIMESH tri;
        CollisionReport::CONTACT p;
        dReal ffar = 1.0f;

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(CO_Contacts|CO_Distance);
        vpoints.reserve(num_samples);

        dReal counter = ffar/sqrt((dReal)num_samples/12);
        for(int k = 0; k < 6; k++) {
            for(dReal i = -ffar/2.0f; i < ffar/2.0f; i+=counter) {
                for(dReal j = -ffar/2.0f; j < ffar/2.0f; j+=counter) {
                    switch(k){
                    case 0:
                        r.pos = Vector(center.x-ffar,center.y+i,center.z+j);
                        r.dir = Vector(1000,0,0);
                        break;
                    case 1:
                        r.pos = Vector(center.x+ffar,center.y+i,center.z+j);
                        r.dir = Vector(-1000,0,0);
                        break;
                    case 2:
                        r.pos = Vector(center.x+i,center.y-ffar,center.z+j);
                        r.dir = Vector(0,1000,0);
                        break;
                    case 3:
                        r.pos = Vector(center.x+i,center.y+ffar,center.z+j);
                        r.dir = Vector(0,-1000,0);
                        break;
                    case 4:
                        r.pos = Vector(center.x+i,center.y+j,center.z-ffar);
                        r.dir = Vector(0,0,1000);
                        break;
                    case 5:
                        r.pos = Vector(center.x+i,center.y+j,center.z+ffar);
                        r.dir = Vector(0,0,-1000);
                        break;
                    }
                
                    if( GetEnv()->CheckCollision(r, KinBodyConstPtr(pbody), _report) ) {
                        p.norm = -_report->contacts.at(0).norm;//-r.dir//_report->contacts.at(0).norm1;
                        p.pos = _report->contacts.at(0).pos;// + 0.001f * p.norm; // extrude a little
                        p.depth = 0;
                        vpoints.push_back(p);
                    }
                }
            }
        }

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(0);
    }

    // computes a distance map. For every point, samples many vectors around the point's normal such that angle
    // between normal and sampled vector doesn't exceeed fTheta. Returns the minimum distance.
    // vpoints needs to already be initialized
    void _ComputeDistanceMap(vector<CollisionReport::CONTACT>& vpoints, dReal fTheta)
    {
        dReal fCosTheta = RaveCos(fTheta);
        int N;
        if(fTheta < 0.01f) {
            N = 1;
        }
        RAY r;

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(CO_Distance);

        // set number of rays to randomly sample
        if( fTheta < 0.01f ) {
            N = 1;
        }
        else {
            N = (int)ceil(fTheta * (64.0f/(PI/12.0f))); // sample 64 points when at pi/12
        }
        for(int i = 0; i < (int)vpoints.size(); ++i) {
            Vector vright = Vector(1,0,0);
            if( RaveFabs(vpoints[i].norm.x) > 0.9 ) {
                vright.y = 1;
            }
            vright -= vpoints[i].norm * vright.dot3(vpoints[i].norm);
            vright.normalize3();
            Vector vup = vpoints[i].norm.cross(vright);

            dReal fMinDist = 2;
            for(int j = 0; j < N; ++j) {
                // sample around a cone
                dReal fAng = fCosTheta + (1-fCosTheta)*RaveRandomFloat();
                dReal R = RaveSqrt(1 - fAng * fAng);
                dReal U2 = 2 * PI * RaveRandomFloat();
                r.dir = 1000.0f*(fAng * vpoints[i].norm + R * RaveCos(U2) * vright + R * RaveSin(U2) * vup);

                r.pos = vpoints[i].pos;

                if( GetEnv()->CheckCollision(r, _report) ) {
                    if( _report->minDistance < fMinDist )
                        fMinDist = _report->minDistance;
                }
            }

            vpoints[i].depth = fMinDist;
        }

        GetEnv()->GetCollisionChecker()->SetCollisionOptions(0);
    }
    
    void _GetStableContacts(vector< pair<CollisionReport::CONTACT,int> >& contacts, const Vector& direction, dReal mu)
    {
        BOOST_ASSERT(mu>0);
        RAVELOG_DEBUG("Starting GetStableContacts...\n");

        if(!GetEnv()->CheckCollision(KinBodyConstPtr(_robot))) {
            RAVELOG_ERROR("GrasperProblem::GetStableContacts - Error: Robot is not colliding with the target.\n");
            return;
        }

        //make sure we get the right closing direction and don't look at irrelevant joints
        vector<dReal> closingdir(_robot->GetDOF(),0);
        FOREACH(itmanip,_robot->GetManipulators()) {
            vector<dReal>::const_iterator itclosing = (*itmanip)->GetClosingDirection().begin();
            FOREACHC(itgripper,(*itmanip)->GetGripperIndices()) {
                closingdir.at(*itgripper) = *itclosing++;
            }
        }

        // calculate the contact normals using the Jacobian
        std::vector<dReal> J;    
        FOREACHC(itlink,_robot->GetLinks()) {
            if( GetEnv()->CheckCollision(KinBody::LinkConstPtr(*itlink), _report) )  {
                RAVELOG_DEBUG(str(boost::format("contact %s:%s with %s:%s\n")%_report->plink1->GetParent()->GetName()%_report->plink1->GetName()%_report->plink2->GetParent()->GetName()%_report->plink2->GetName()));
                FOREACH(itcontact, _report->contacts) {  
                    if( _report->plink1 != *itlink )
                        itcontact->norm = -itcontact->norm;

                    Vector deltaxyz;
                    //check if this link is the base link, if so there will be no Jacobian
                    if( *itlink == _robot->GetLinks().at(0) || (!!_robot->GetActiveManipulator() && *itlink == _robot->GetActiveManipulator()->GetBase()) )  {
                        deltaxyz = direction;
                    }
                    else {
                        //calculate the jacobian for the contact point as if were part of the link
                        Transform pointTm;
                        pointTm.trans = itcontact->pos;
                        _robot->CalculateJacobian((*itlink)->GetIndex(), pointTm.trans, J);

                        //get the vector of delta xyz induced by a small squeeze for all joints relevant manipulator joints
                        for(int j = 0; j < 3; j++) {
                            for(int k = 0; k < _robot->GetDOF(); k++)
                                deltaxyz[j] += J.at(j*_robot->GetDOF() + k)*closingdir[k];
                        }
                    }
                
                    //if ilink is degenerate to base link (no joint between them), deltaxyz will be 0 0 0
                    //so treat it as if it were part of the base link
                    if(deltaxyz.lengthsqr3() < 1e-7f) {
                        RAVELOG_WARN(str(boost::format("degenerate link at %s")%(*itlink)->GetName()));
                        deltaxyz = direction;
                    }
                
                    deltaxyz.normalize3();

                    if( IS_DEBUGLEVEL(Level_Debug) ) {
                        stringstream ss; 
                        ss << "link " << (*itlink)->GetIndex() << " delta XYZ: ";
                        for(int q = 0; q < 3; q++)
                            ss << deltaxyz[q] << " ";
                        ss << endl;
                        RAVELOG_DEBUG(ss.str());
                    }

                    // determine if contact is stable (if angle is obtuse, can't be in friction cone)
                    dReal fsin2 = itcontact->norm.cross(deltaxyz).lengthsqr3();
                    dReal fcos = itcontact->norm.dot3(deltaxyz);
                    bool bstable = fcos > 0 && fsin2 <= fcos*fcos*mu*mu;
                    if(bstable)
                        contacts.push_back(make_pair(*itcontact,(*itlink)->GetIndex()));
                }
            }
        }
    }

    virtual GRASPANALYSIS _AnalyzeContacts3D(const vector<CollisionReport::CONTACT>& contacts, dReal mu, int Nconepoints)
    {
        if( mu == 0 )
            return _AnalyzeContacts3D(contacts);

        dReal fdeltaang = 2*PI/(dReal)Nconepoints;
        dReal fang = 0;
        vector<pair<dReal,dReal> > vsincos(Nconepoints);
        FOREACH(it,vsincos) {
            it->first = RaveSin(fang);
            it->second = RaveCos(fang);
            fang += fdeltaang;
        }
        
        vector<CollisionReport::CONTACT> newcontacts;
        newcontacts.reserve(contacts.size()*Nconepoints);
        FOREACHC(itcontact,contacts) {
            // find a coordinate system where z is the normal
            TransformMatrix torient = matrixFromQuat(quatRotateDirection(Vector(0,0,1),itcontact->norm));
            Vector right(torient.m[0],torient.m[4],torient.m[8]);
            Vector up(torient.m[1],torient.m[5],torient.m[9]);
            FOREACH(it,vsincos)
                newcontacts.push_back(CollisionReport::CONTACT(itcontact->pos, (itcontact->norm + mu*it->first*right + mu*it->second*up).normalize3(),0));
        }

        return _AnalyzeContacts3D(newcontacts);
    }

    virtual GRASPANALYSIS _AnalyzeContacts3D(const vector<CollisionReport::CONTACT>& contacts)
    {
        if( contacts.size() < 7 )
            throw openrave_exception("need at least 7 contact wrenches to have force closure in 3D");

        GRASPANALYSIS analysis;
        vector<double> vpoints(6*contacts.size()), vconvexplanes;
        vector<double>::iterator itpoint = vpoints.begin();
        FOREACHC(itcontact, contacts) {
            *itpoint++ = itcontact->norm.x;
            *itpoint++ = itcontact->norm.y;
            *itpoint++ = itcontact->norm.z;
            Vector v = itcontact->pos.cross(itcontact->norm);
            *itpoint++ = v.x;
            *itpoint++ = v.y;
            *itpoint++ = v.z;
        }

        analysis.volume = _ComputeConvexHull(vpoints,vconvexplanes,boost::shared_ptr< vector<int> >(),6);

        // go through each of the faces and check if center is inside, and compute its distance
        double mindist = 1e30;
        for(size_t i = 0; i < vconvexplanes.size(); i += 7) {
            if( vconvexplanes.at(i+6) > 0 || RaveFabs(vconvexplanes.at(i+6)) < 1e-15 )
                return analysis;
            mindist = min(mindist,-vconvexplanes.at(i+6));
        }
        analysis.mindist = mindist;
        return analysis;
    }

    /// Computes the convex hull of a set of points
    /// \param vpoints a set of points each of dimension dim
    /// \param vconvexplaces the places of the convex hull, dimension is dim+1
    virtual double _ComputeConvexHull(const vector<double>& vpoints, vector<double>& vconvexplanes, boost::shared_ptr< vector<int> > vconvexfaces, int dim)
    {
        vconvexplanes.resize(0);
#ifdef QHULL_FOUND
        vector<coordT> qpoints(vpoints.size());
        std::copy(vpoints.begin(),vpoints.end(),qpoints.begin());
        
        boolT ismalloc = 0;           // True if qhull should free points in qh_freeqhull() or reallocation
        char flags[]= "qhull Tv FA"; // option flags for qhull, see qh_opt.htm, output volume (FA)

        if( !errfile ) {
            errfile = tmpfile();    // stderr, error messages from qhull code  
        }
        int exitcode= qh_new_qhull (dim, qpoints.size()/dim, &qpoints[0], ismalloc, flags, errfile, errfile);
        if (!exitcode) {
            vconvexplanes.reserve(1000);
            if( !!vconvexfaces ) {
                // fill with face indices
                vconvexfaces->resize(0);
                vconvexfaces->push_back(0);
            }
            facetT *facet;	          // set by FORALLfacets 
            vertexT *vertex, **vertexp; // set by FOREACHvdertex_
            FORALLfacets { // 'qh facet_list' contains the convex hull
//                if( facet->isarea && facet->f.area < 1e-15 ) {
//                    RAVELOG_VERBOSE(str(boost::format("skipping area: %e\n")%facet->f.area));
//                    continue;
//                }
                if( !!vconvexfaces && !!facet->vertices ) {
                    size_t startindex = vconvexfaces->size();
                    vconvexfaces->push_back(0);
                    FOREACHvertex_(facet->vertices) {
                        int id = qh_pointid(vertex->point);
                        BOOST_ASSERT(id>=0);
                        vconvexfaces->push_back(id);
                    }
                    vconvexfaces->at(startindex) = vconvexfaces->size()-startindex-1;
                    vconvexfaces->at(0) += 1;
                }
                if( !!facet->normal ) {
                    for(int i = 0; i < dim; ++i) {
                        vconvexplanes.push_back(facet->normal[i]);
                    }
                    vconvexplanes.push_back(facet->offset);
                }
            }
        }
        
        double totvol = qh totvol;
        qh_freeqhull(!qh_ALL);
        int curlong, totlong;	  // memory remaining after qh_memfreeshort 
        qh_memfreeshort (&curlong, &totlong);
        if (curlong || totlong) {
            RAVELOG_ERROR("qhull internal warning (main): did not free %d bytes of long memory (%d pieces)\n", totlong, curlong);
        }
        if( exitcode ) {
            throw openrave_exception(str(boost::format("Qhull failed with error %d")%exitcode));
        }

        vector<double> vmean(dim,0);
        for(size_t i = 0; i < vpoints.size(); i += dim) {
            for(int j = 0; j < dim; ++j)
                vmean[j] += vpoints[i+j];
        }
        double fipoints = 1/(double)(vpoints.size()/dim);
        for(int j = 0; j < dim; ++j) {
            vmean[j] *= fipoints;
        }        
        for(size_t i = 0; i < vconvexplanes.size(); i += dim+1) {
            double meandist = 0;
            for(int j = 0; j < dim; ++j) {
                meandist += vconvexplanes[i+j]*vmean[j];
            }
            meandist += vconvexplanes.at(i+dim);
            if( meandist > 0 ) {
                for(int j = 0; j < dim; ++j) {
                    vconvexplanes[i+j] = -vconvexplanes[i+j];
                }
            }
        }

        return totvol; // return volume
#else
        throw openrave_exception(str(boost::format("QHull library not found, cannot compute convex hull of contact points")));
        return 0;
#endif
    }

    PlannerBasePtr _planner;
    RobotBasePtr _robot;
    CollisionReportPtr _report;
    boost::mutex _mutex;
    FILE *errfile;
};

#endif