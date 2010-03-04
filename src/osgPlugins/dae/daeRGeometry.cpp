/*
 * Copyright 2006 Sony Computer Entertainment Inc.
 *
 * Licensed under the SCEA Shared Source License, Version 1.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at:
 * http://research.scea.com/scea_shared_source_license.html
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing permissions and limitations under the
 * License.
 */

#include "daeReader.h"
#include "domSourceReader.h"
#include <dae.h>
#include <dom/domCOLLADA.h>
#include <dom/domInstance_geometry.h>
#include <dom/domInstance_controller.h>
#include <dom/domController.h>
#include <dom/domConstants.h>
#include <osg/StateSet>
#include <osg/ShapeDrawable>
#include <osg/Geometry>

#include <osgAnimation/MorphGeometry>
#include <osgAnimation/RigGeometry>
#include <osgAnimation/UpdateBone>

using namespace osgDAE;

osg::Geode* daeReader::getOrCreateGeometry(domGeometry *pDomGeometry, domBind_material* pDomBindMaterial, const osg::Geode** ppOriginalGeode)
{
    // Check cache if geometry already exists
    osg::Geode* pOsgGeode;

    domGeometryGeodeMap::iterator iter = _geometryMap.find( pDomGeometry );
    if ( iter != _geometryMap.end() )
    {
        pOsgGeode = iter->second.get();
    }
    else
    {
        pOsgGeode = processGeometry( pDomGeometry );
        _geometryMap.insert( std::make_pair( pDomGeometry, pOsgGeode ) );
    }

    if (ppOriginalGeode)
    {
        *ppOriginalGeode = pOsgGeode;
    }

    if (!pOsgGeode)
        return NULL;

    // Create a copy of the cached Geode with a copy of the drawables,
    // because we may be using a different material or texture unit bindings.
    osg::Geode *pCopiedOsgGeode = static_cast<osg::Geode*>(pOsgGeode->clone(osg::CopyOp::DEEP_COPY_DRAWABLES));
    if ( pCopiedOsgGeode == NULL )
    {
        osg::notify( osg::WARN ) << "Failed to load geometry " << pDomGeometry->getName() << std::endl;
        return NULL;
    }

    // Compute optimized geometry by expanding all indexed arrays so we are no longer rendering with the slow path
    for(unsigned int i=0;i < pCopiedOsgGeode->getNumDrawables();++i)
    {
        osg::Geometry* geom = pCopiedOsgGeode->getDrawable(i)->asGeometry();
        if (geom)
        {
            if (!geom->areFastPathsUsed() && !geom->getInternalOptimizedGeometry())
            {
                //geom->computeInternalOptimizedGeometry();
            }
        }
    }

    if (pDomBindMaterial)
    {
        processBindMaterial( pDomBindMaterial, pDomGeometry, pCopiedOsgGeode, pOsgGeode );
    }

    return pCopiedOsgGeode;
}

osgAnimation::Bone* daeReader::getOrCreateBone(domNode *pDomNode)
{
    // Check cache if bone already exists
    osgAnimation::Bone *pOsgBone = NULL;

    domNodeOsgBoneMap::iterator iterBone = _jointMap.find( pDomNode );
    if ( iterBone != _jointMap.end() )
        return iterBone->second.get();

    std::string name;
    if (pDomNode->getId())
        name = pDomNode->getId();
    if (name.empty() && pDomNode->getSid())
        name = pDomNode->getSid();
    if (name.empty() && pDomNode->getName())
        name = pDomNode->getName();
    pOsgBone = new osgAnimation::Bone(name);
    pOsgBone->setDataVariance(osg::Object::DYNAMIC);

    pOsgBone->setUpdateCallback(new osgAnimation::UpdateBone(name));

    _jointMap.insert( std::make_pair( pDomNode, pOsgBone ) );

    return pOsgBone;
}

osgAnimation::Skeleton* daeReader::getOrCreateSkeleton(domNode *pDomNode)
{
    // Check cache if skeleton already exists
    osgAnimation::Skeleton *pOsgSkeleton = NULL;

    domNodeOsgSkeletonMap::iterator iter = _skeletonMap.find( pDomNode );
    if ( iter != _skeletonMap.end() )
        return iter->second.get();

    pOsgSkeleton = new osgAnimation::Skeleton;
    pOsgSkeleton->setDefaultUpdateCallback();
    pOsgSkeleton->setDataVariance(osg::Object::DYNAMIC);

    _skeletonMap.insert( std::make_pair( pDomNode, pOsgSkeleton ) );

    return pOsgSkeleton;
}



osg::Geode* daeReader::processInstanceGeometry( domInstance_geometry *pDomInstanceGeometry )
{
    domGeometry *pDomGeometry = daeSafeCast< domGeometry >(getElementFromURI(pDomInstanceGeometry->getUrl()));
    if (!pDomGeometry)
    {
        osg::notify( osg::WARN ) << "Failed to locate geometry " << pDomInstanceGeometry->getUrl().getURI() << std::endl;
        return NULL;
    }

    return getOrCreateGeometry(pDomGeometry, pDomInstanceGeometry->getBind_material());
}

// <morph source (method)>
// 2..*    <source>
// 1    <targets>
//        2..*    <input semantic source>
//        0..*    <extra>
// 0..* <extra>
osg::Node* daeReader::processMorph(domMorph* pDomMorph, domBind_material* pDomBindMaterial)
{
    domGeometry* pDomGeometry = daeSafeCast< domGeometry >(getElementFromURI( pDomMorph->getSource()));

    if (!pDomGeometry)
    {
        osg::notify( osg::WARN ) << "Failed to locate geometry " << pDomMorph->getSource().getURI() << std::endl;
        return NULL;
    }

    // Base mesh
    osg::Geode* pOsgGeode = getOrCreateGeometry(pDomGeometry, pDomBindMaterial);
    if (!pOsgGeode)
        return NULL;

    // Expects a single geometry inside the geode, should change this
    osg::Geometry* pOsgGeometry = dynamic_cast<osg::Geometry*>(pOsgGeode->getDrawable(0));
    if (!pOsgGeometry)
        return NULL;

    osgAnimation::MorphGeometry* pOsgMorphGeometry = new osgAnimation::MorphGeometry(*pOsgGeometry);
    pOsgGeode->removeDrawables(0);
    pOsgGeode->addDrawable(pOsgMorphGeometry);

    domMorphMethodType morphMethod = pDomMorph->getMethod();

    //Files exported by the FBX converter always seem to say they're relative
    //when in fact they should be normalized.
    if (_authoringTool == FBX_CONVERTER)
    {
        morphMethod = MORPHMETHODTYPE_NORMALIZED;
    }

    switch (morphMethod)
    {
    case MORPHMETHODTYPE_RELATIVE:
        pOsgMorphGeometry->setMethod(osgAnimation::MorphGeometry::RELATIVE);
        break;
    case MORPHMETHODTYPE_NORMALIZED:
        pOsgMorphGeometry->setMethod(osgAnimation::MorphGeometry::NORMALIZED);
        break;
    default:
        osg::notify( osg::WARN ) << "Unknown morph method method type " << std::endl;
    }

    // 1    <targets>
    domMorph::domTargets* pDomMorhpTargets = pDomMorph->getTargets();
    domInputLocal_Array domInputs = pDomMorhpTargets->getInput_array();

    // TODO how to handle multiple pairs of morph inputs?
    if (domInputs.getCount() > 2)
    {
        osg::notify( osg::WARN ) << "Only a single pair of morph inputs is supported." << std::endl;
    }

    for (size_t i=0; i < 2; i++)
    {
        if (!strcmp(domInputs[i]->getSemantic(), COMMON_PROFILE_INPUT_MORPH_TARGET))
        {
            domSource* pDomSource = daeSafeCast<domSource>(getElementFromURI(domInputs[i]->getSource()));
            if (pDomSource)
            {
                if (const domName_array* pDomNames = pDomSource->getName_array())
                {
                    const domListOfNames& names = pDomNames->getValue();
                    for (size_t j=0; j < names.getCount(); j++)
                    {
                        daeSIDResolver resolver(_visualScene, names.get(j));
                        pDomGeometry = daeSafeCast< domGeometry >(resolver.getElement());

                        if (pDomGeometry)
                        {
                            osg::Geode* targetgeode = getOrCreateGeometry(pDomGeometry, NULL);

                            // Expects a single geometry inside the geode, should change this
                            osg::Geometry* pOsgGeometry = dynamic_cast<osg::Geometry*>(targetgeode->getDrawable(0));
                            if (pOsgGeometry)
                            {
                                pOsgMorphGeometry->addMorphTarget(pOsgGeometry);
                            }
                        }
                        else
                        {
                            osg::notify( osg::WARN ) << "Failed to locate morph geometry '" << names.get(j) << "'" << std::endl;
                        }
                    }
                }
                else if (domIDREF_array* pDomIDREFs = pDomSource->getIDREF_array())
                {
                    xsIDREFS* pIDREFS = &(pDomIDREFs->getValue());
                    for (size_t j=0; j < pIDREFS->getCount(); j++)
                    {
                        pDomGeometry = daeSafeCast< domGeometry >(getElementFromIDRef(pIDREFS->get(j)));

                        if (pDomGeometry)
                        {
                            osg::Geode* targetgeode = getOrCreateGeometry(pDomGeometry, NULL);

                            // Expects a single geometry inside the geode, should change this
                            osg::Geometry* pOsgGeometry = dynamic_cast<osg::Geometry*>(targetgeode->getDrawable(0));
                            if (pOsgGeometry)
                            {
                                pOsgMorphGeometry->addMorphTarget(pOsgGeometry);
                            }
                        }
                        else
                        {
                            osg::notify( osg::WARN ) << "Failed to locate morph geometry '" << pIDREFS->get(j).getID() << "'" << std::endl;
                        }
                    }
                }
            }
            else
            {
                osg::notify( osg::WARN ) << "Could not find morph source '" << domInputs[i]->getSource().getURI() << "'" <<std::endl;
                return NULL;
            }
        }
        else if (!strcmp(domInputs[i]->getSemantic(), COMMON_PROFILE_INPUT_MORPH_WEIGHT))
        {
            domSource* pDomSource = daeSafeCast<domSource>(getElementFromURI(domInputs[i]->getSource()));
            if (pDomSource)
            {
                domFloat_array* pDomFloatArray = pDomSource->getFloat_array();
                domListOfFloats weights = pDomFloatArray->getValue();
                for (size_t j=0; j < pDomFloatArray->getCount(); j++)
                {
                    pOsgMorphGeometry->setWeight(j, weights.get(j));
                }

                // See if morph weights are targetted by animations
                daeElementDomChannelMap::iterator iter = _daeElementDomChannelMap.find(pDomSource);
                if (iter != _daeElementDomChannelMap.end())
                {
                    std::string name = pDomSource->getId() ? pDomSource->getId() : "";
                    osgAnimation::UpdateMorph* pUpdateCallback = new osgAnimation::UpdateMorph(name);
                    pOsgGeode->setUpdateCallback(pUpdateCallback);
                    pOsgGeode->setDataVariance(osg::Object::DYNAMIC);

                    // Associate all animation channels with this update callback
                    do
                    {
                        _domChannelOsgAnimationUpdateCallbackMap[iter->second] = pUpdateCallback;
                        ++iter;
                    }
                    while (iter != _daeElementDomChannelMap.upper_bound(pDomSource));
                }
            }
            else
            {
                osg::notify( osg::WARN ) << "Could not find morph source '" << domInputs[i]->getSource().getURI() << "'" <<std::endl;
                return NULL;
            }
        }
    }

    return pOsgGeode;
}

// <controller (id) (name)>
// 0..1 <asset>
// 1    <skin>, <morph>
// 0..* <extra>
osg::Node* daeReader::processInstanceController( domInstance_controller *pDomInstanceController )
{
    domController *pDomController = daeSafeCast< domController >(getElementFromURI(pDomInstanceController->getUrl()));
    if (!pDomController)
    {
        osg::notify( osg::WARN ) << "Failed to locate controller " << pDomInstanceController->getUrl().getURI() << std::endl;
        return NULL;
    }

    if (pDomController->getSkin())
    {
        _skinInstanceControllers.push_back(pDomInstanceController);
        return NULL;
    }
    else if (pDomController->getMorph())
    {
        return processMorph(pDomController->getMorph(), pDomInstanceController->getBind_material());
    }

    osg::notify( osg::WARN ) << "Expected skin or morph element in controller '" << pDomController->getName() << "'" << std::endl;

    return NULL;
}

// <mesh>
// 1..* <source>
// 1    <vertices>
// 0..*    <lines>, <linestrips>, <polygons>, <polylist>, <triangles>, <trifans>, <tristrips>
// 0..* <extra>
osg::Geode *daeReader::processMesh(domMesh* pDomMesh)
{
    osg::Geode* pOsgGeode = new osg::Geode;
//    if (pDomMesh->getId() != NULL )
    {
//        pOsgGeode->setName( pDomMesh->getId() );
    }

    // size_t count = mesh->getContents().getCount();

    // 1..* <source>
    SourceMap sources;
    domSource_Array sourceArray = pDomMesh->getSource_array();
    for ( size_t i = 0; i < sourceArray.getCount(); i++)
    {
        sources.insert(std::make_pair((daeElement*)sourceArray[i], domSourceReader(sourceArray[i])));
    }

    // 0..*    <lines>
    domLines_Array linesArray = pDomMesh->getLines_array();
    for ( size_t i = 0; i < linesArray.getCount(); i++)
    {
        processSinglePPrimitive<domLines>(pOsgGeode, pDomMesh, linesArray[i], sources, GL_LINES);
    }

    // 0..*    <linestrips>
    domLinestrips_Array linestripsArray = pDomMesh->getLinestrips_array();
    for ( size_t i = 0; i < linestripsArray.getCount(); i++)
    {
        processMultiPPrimitive<domLinestrips>(pOsgGeode, pDomMesh, linestripsArray[i], sources, GL_LINE_STRIP);
    }

    // 0..* <polygons>
    domPolygons_Array polygonsArray = pDomMesh->getPolygons_array();
    for ( size_t i = 0; i < polygonsArray.getCount(); i++)
    {
        processPolygons<domPolygons>(pOsgGeode, pDomMesh, polygonsArray[i], sources);
    }

    // 0..* <polylist>
    domPolylist_Array polylistArray = pDomMesh->getPolylist_array();
    for ( size_t i = 0; i < polylistArray.getCount(); i++)
    {
        processPolylist(pOsgGeode, pDomMesh, polylistArray[i], sources);
    }

    // 0..* <triangles>
    domTriangles_Array trianglesArray = pDomMesh->getTriangles_array();
    for ( size_t i = 0; i < trianglesArray.getCount(); i++)
    {
        processSinglePPrimitive<domTriangles>(pOsgGeode, pDomMesh, trianglesArray[i], sources, GL_TRIANGLES);
    }

    // 0..* <trifans>
    domTrifans_Array trifansArray = pDomMesh->getTrifans_array();
    for ( size_t i = 0; i < trifansArray.getCount(); i++)
    {
        processPolygons<domTrifans>(pOsgGeode, pDomMesh, trifansArray[i], sources);
    }

    // 0..* <tristrips>
    domTristrips_Array tristripsArray = pDomMesh->getTristrips_array();
    for ( size_t i = 0; i < tristripsArray.getCount(); i++)
    {
        processMultiPPrimitive<domTristrips>(pOsgGeode, pDomMesh, tristripsArray[i], sources, GL_TRIANGLE_STRIP);
    }

    return pOsgGeode;
}

// <convexmesh>
osg::Geode *daeReader::processConvexMesh(domConvex_mesh* pDomConvexMesh)
{
//    osg::notify( osg::WARN ) << "Unsupported geometry convex mesh '" << pDomConvexMesh->getId() << "'" << std::endl;
    return NULL;
}

// <spline>
osg::Geode *daeReader::processSpline(domSpline* pDomSpline)
{
//    osg::notify( osg::WARN ) << "Unsupported geometry type spline '" << pDomSpline->getId() << "'" << std::endl;
    return NULL;
}

// <geometry (id) (name)>
// 0..1 <asset>
// 1    <convex_mesh>, <mesh>, <spline>
// 0..* <extra>
osg::Geode *daeReader::processGeometry(domGeometry *pDomGeometry)
{
    if (pDomGeometry->getMesh())
    {
        return processMesh(pDomGeometry->getMesh());
    }
    else if (pDomGeometry->getConvex_mesh())
    {
        return processConvexMesh(pDomGeometry->getConvex_mesh());
    }
    else if (pDomGeometry->getSpline())
    {
        return processSpline(pDomGeometry->getSpline());
    }
#ifdef COLLADA15
    else if (pDomGeometry->getBRep())
    {
        return processBRep(pDomGeometry->getBRep());
    }
#endif

    osg::notify( osg::WARN ) << "Unexpected geometry type in geometry '" << pDomGeometry->getId() << "'" << std::endl;
    return NULL;
}


template< typename T >
void daeReader::processSinglePPrimitive(osg::Geode* geode,
    const domMesh* pDomMesh, const T* group, SourceMap& sources, GLenum mode)
{
    osg::Geometry *geometry = new osg::Geometry();
    geometry->setName(group->getMaterial());
    geode->addDrawable( geometry );

    osg::DrawElementsUInt* pDrawElements = new osg::DrawElementsUInt(mode);
    geometry->addPrimitiveSet(pDrawElements);

    domP_Array domPArray;
    domPArray.append(group->getP());
    std::vector<std::vector<GLuint> > indexLists;
    resolveMeshArrays(domPArray, group->getInput_array(), pDomMesh,
        geometry, sources, indexLists);
    pDrawElements->asVector().swap(indexLists.front());
}

template< typename T >
void daeReader::processMultiPPrimitive(osg::Geode* geode,
    const domMesh* pDomMesh, const T* group, SourceMap &sources, GLenum mode)
{
    osg::Geometry *geometry = new osg::Geometry();
    geometry->setName(group->getMaterial());
    geode->addDrawable( geometry );

    std::vector<std::vector<GLuint> > indexLists;
    resolveMeshArrays(group->getP_array(), group->getInput_array(), pDomMesh,
        geometry, sources, indexLists);

    for (size_t i = 0; i < indexLists.size(); ++i)
    {
        osg::DrawElementsUInt* pDrawElements = new osg::DrawElementsUInt(mode);
        geometry->addPrimitiveSet(pDrawElements);
        pDrawElements->asVector().swap(indexLists[i]);
    }
}

void daeReader::processPolylist(osg::Geode* geode, const domMesh* pDomMesh, const domPolylist *group, SourceMap &sources)
{
    const domPolylist::domVcount* pDomVcount = group->getVcount();
    if (!pDomVcount)
    {
        osg::notify(osg::WARN) << "Index counts not found." << std::endl;
        return;
    }

    osg::Geometry* geometry = new osg::Geometry();
    geometry->setName(group->getMaterial());
    geode->addDrawable(geometry);

    std::vector<std::vector<GLuint> > vertexLists;
    domP_Array domPArray;
    domPArray.append(group->getP());
    resolveMeshArrays(domPArray, group->getInput_array(), pDomMesh, geometry, sources, vertexLists);

    const std::vector<GLuint>& vertexList = vertexLists.front();

    osg::DrawElementsUInt* pDrawTriangles = new osg::DrawElementsUInt(GL_TRIANGLES);
    geometry->addPrimitiveSet(pDrawTriangles);

    const domListOfUInts& vCount = pDomVcount->getValue();
    for (size_t i = 0, j = 0; i < vCount.getCount(); ++i)
    {
        size_t primitiveLength = vCount[i];
        if (j + primitiveLength > vertexList.size())
        {
            osg::notify(osg::WARN) << "Error: vertex counts are greater than the number of indices." << std::endl;
            return;
        }
        for (size_t k = 2; k < primitiveLength; ++k)
        {
            pDrawTriangles->push_back(vertexList[j]);
            pDrawTriangles->push_back(vertexList[j+k-1]);
            pDrawTriangles->push_back(vertexList[j+k]);
        }
        j += primitiveLength;
    }
}

template <typename T>
void daeReader::processPolygons(osg::Geode* geode,
    const domMesh* pDomMesh, const T *group, SourceMap& sources)
{
    osg::Geometry *geometry = new osg::Geometry();
    geometry->setName(group->getMaterial());
    geode->addDrawable(geometry);

    osg::DrawElementsUInt* pDrawElements = new osg::DrawElementsUInt(GL_TRIANGLES);
    geometry->addPrimitiveSet(pDrawElements);

    std::vector<std::vector<GLuint> > indexLists;
    resolveMeshArrays(group->getP_array(), group->getInput_array(), pDomMesh,
        geometry, sources, indexLists);

    for ( size_t i = 0; i < indexLists.size(); ++i)
    {
        const std::vector<GLuint>& indices = indexLists[i];

        for (size_t j = 2; j < indices.size(); ++j)
        {
            pDrawElements->push_back(indices.front());
            pDrawElements->push_back(indices[j - 1]);
            pDrawElements->push_back(indices[j]);
        }
    }
}

void processVertices(
    domVertices* vertices,
    daeElement*& position_source,
    daeElement*& color_source,
    daeElement*& normal_source,
    daeElement*& texcoord_source)
{
    const domInputLocal_Array& inputs = vertices->getInput_array();

    // Process input elements within the vertices element. These are of the unshared type
    // and therefore cannot have set and offset attributes

    for (size_t i = 0; i < inputs.getCount(); ++i)
    {
        xsNMTOKEN semantic = inputs[i]->getSemantic();
        daeElement* pDaeElement = getElementFromURI(inputs[i]->getSource());
        if (strcmp(COMMON_PROFILE_INPUT_POSITION, semantic) == 0)
        {
            position_source = pDaeElement;
        }
        else if (strcmp(COMMON_PROFILE_INPUT_COLOR, semantic) == 0)
        {
            color_source = pDaeElement;
        }
        else if (strcmp(COMMON_PROFILE_INPUT_NORMAL, semantic) == 0)
        {
            normal_source = pDaeElement;
        }
        else if (strcmp(COMMON_PROFILE_INPUT_TEXCOORD, semantic) == 0)
        {
            texcoord_source = pDaeElement;
        }
    }
}

// I've never seen more than 2 used so this should be enough. If you find that
// a greater number is needed then increase it accordingly and submit the change
// to OpenSceneGraph.
// Why not use a vector? Because a large map of VertexIndices is used and
// allocating vectors for each element would make it a lot slower.
const unsigned int MAX_TEXTURE_COORDINATE_SETS = 4;

void resolveMeshInputs(
    const domInputLocalOffset_Array &inputs,
    daeElement*& position_source,
    daeElement*& color_source,
    daeElement*& normal_source,
    daeElement* texcoord_sources[MAX_TEXTURE_COORDINATE_SETS],
    int& position_offset,
    int& color_offset,
    int& normal_offset,
    int texcoord_offsets[MAX_TEXTURE_COORDINATE_SETS])
{
    position_source = color_source = normal_source = NULL;
    position_offset = color_offset = normal_offset = 0;
    for (int i = 0; i < MAX_TEXTURE_COORDINATE_SETS; ++i)
    {
        texcoord_sources[i] = NULL;
        texcoord_offsets[i] = NULL;
    }

    for ( size_t i = 0; i < inputs.getCount(); i++ )
    {
        if (strcmp(COMMON_PROFILE_INPUT_VERTEX, inputs[i]->getSemantic()) == 0)
        {
            daeElement* pDaeElement = getElementFromURI(inputs[i]->getSource());
            if (domVertices* vertices = daeSafeCast<domVertices>(pDaeElement))
            {
                processVertices(vertices, position_source, color_source, normal_source, texcoord_sources[0]);
                position_offset = inputs[i]->getOffset();

                if (color_source) color_offset = position_offset;
                if (normal_source) normal_offset = position_offset;
                if (texcoord_sources[0]) texcoord_offsets[0] = position_offset;
            }
            break;
        }
    }

    for ( size_t i = 0; i < inputs.getCount(); i++ )
    {
        xsNMTOKEN semantic = inputs[i]->getSemantic();
        daeElement* pDaeElement = getElementFromURI(inputs[i]->getSource());
        int offset = inputs[i]->getOffset();

        if (strcmp(COMMON_PROFILE_INPUT_COLOR, semantic) == 0)
        {
            if (color_source != NULL)
                osg::notify( osg::WARN )<<"Overwriting vertices input(COLOR) with input from primitive"<<std::endl;
            color_source = pDaeElement;
            color_offset = offset;
        }
        else if (strcmp(COMMON_PROFILE_INPUT_NORMAL, semantic) == 0)
        {
            if (normal_source != NULL)
                osg::notify( osg::WARN )<<"Overwriting vertices input(NORMAL) with input from primitive"<<std::endl;
            normal_source = pDaeElement;
            normal_offset = offset;
        }
        else if (strcmp(COMMON_PROFILE_INPUT_TEXCOORD, semantic) == 0)
        {
            unsigned set = inputs[i]->getSet();
            if (set >= MAX_TEXTURE_COORDINATE_SETS)
            {
                osg::notify( osg::WARN )<<"Texture coordinate set "<< set <<
                    "was requested, the maximum allowed is " << MAX_TEXTURE_COORDINATE_SETS - 1 << "." << std::endl;
                continue;
            }
            if (texcoord_sources[set])
                osg::notify( osg::WARN )<<"Overwriting vertices input(TEXCOORD) with input from primitive"<<std::endl;

            texcoord_sources[set] = pDaeElement;
            texcoord_offsets[set] = offset;
        }
    }
}

struct VertexIndices
{
    VertexIndices(int p, int c, int n, const int t[MAX_TEXTURE_COORDINATE_SETS])
        : position_index(p), color_index(c), normal_index(n)
    {
        for (int i = 0; i < MAX_TEXTURE_COORDINATE_SETS; ++i) texcoord_indices[i] = t[i];
    }
    bool operator < (const VertexIndices& rhs) const
    {
        if (position_index != rhs.position_index) return position_index < rhs.position_index;
        if (color_index != rhs.color_index) return color_index < rhs.color_index;
        if (normal_index != rhs.normal_index) return normal_index < rhs.normal_index;
        for (int i = 0; i < MAX_TEXTURE_COORDINATE_SETS; ++i)
        {
            if (texcoord_indices[i] != rhs.texcoord_indices[i]) return texcoord_indices[i] < rhs.texcoord_indices[i];
        }
        return false;
    }

    int position_index, color_index, normal_index, texcoord_indices[MAX_TEXTURE_COORDINATE_SETS];
};

void daeReader::resolveMeshArrays(const domP_Array& domPArray,
    const domInputLocalOffset_Array& inputs, const domMesh* pDomMesh,
    osg::Geometry* geometry, SourceMap &sources,
    std::vector<std::vector<GLuint> >& vertexLists)
{
    daeElement* position_source = NULL;
    daeElement* color_source = NULL;
    daeElement* normal_source = NULL;
    daeElement* texcoord_sources[MAX_TEXTURE_COORDINATE_SETS] = {NULL};
    int position_offset = 0;
    int color_offset = 0;
    int normal_offset = 0;
    int texcoord_offsets[MAX_TEXTURE_COORDINATE_SETS] = {0};

    resolveMeshInputs(inputs,
        position_source,
        color_source,
        normal_source,
        texcoord_sources,
        position_offset,
        color_offset,
        normal_offset,
        texcoord_offsets);

    unsigned stride = 0;
    for (size_t i = 0; i < inputs.getCount(); ++i)
    {
        stride = osg::maximum<unsigned>(stride, inputs[i]->getOffset());
    }
    ++stride;

    typedef std::map<VertexIndices, GLuint> VertexIndicesIndexMap;
    VertexIndicesIndexMap vertexIndicesIndexMap;

    for (size_t j = 0; j < domPArray.getCount(); ++j)
    {
        const domListOfUInts& p = domPArray[j]->getValue();

        for (size_t i = 0; i < p.getCount(); i += stride)
        {
            int texcoord_indices[MAX_TEXTURE_COORDINATE_SETS];
            for (int t = 0; t < MAX_TEXTURE_COORDINATE_SETS; ++t)
            {
                texcoord_indices[t] = p.get(i + texcoord_offsets[t]);
            }
            VertexIndices v(
                p.get(i + position_offset),
                p.get(i + color_offset),
                p.get(i + normal_offset),
                texcoord_indices);
            vertexIndicesIndexMap.insert(VertexIndicesIndexMap::value_type(v, 0));
        }
    }

    {
        VertexIndicesIndexMap::iterator it = vertexIndicesIndexMap.begin(), end = vertexIndicesIndexMap.end();
        for (GLuint i = 0; it != end; ++it, ++i)
        {
            it->second = i;
        }
    }

    vertexLists.resize(domPArray.getCount());

    for (size_t j = 0; j < domPArray.getCount(); ++j)
    {
        const domListOfUInts& p = domPArray[j]->getValue();

        for (size_t i = 0; i < p.getCount(); i += stride)
        {
            int texcoord_indices[MAX_TEXTURE_COORDINATE_SETS];
            for (int t = 0; t < MAX_TEXTURE_COORDINATE_SETS; ++t)
            {
                texcoord_indices[t] = p.get(i + texcoord_offsets[t]);
            }
            VertexIndices v(
                p.get(i + position_offset),
                p.get(i + color_offset),
                p.get(i + normal_offset),
                texcoord_indices);

            GLuint index = vertexIndicesIndexMap.find(v)->second;

            _oldToNewIndexMap.insert(OldToNewIndexMap::value_type(
                OldToNewIndexMap::key_type(pDomMesh, v.position_index),
                OldToNewIndexMap::mapped_type(geometry, index)));
            vertexLists[j].push_back(index);
        }
    }

    if (const osg::Vec3Array* source = sources[position_source].getVec3Array())
    {
        osg::Vec3Array* pArray = new osg::Vec3Array;

        for (VertexIndicesIndexMap::const_iterator it = vertexIndicesIndexMap.begin(),
            end = vertexIndicesIndexMap.end(); it != end; ++it)
        {
            pArray->push_back(source->at(it->first.position_index));
        }

        geometry->setVertexData(osg::Geometry::ArrayData(pArray, osg::Geometry::BIND_PER_VERTEX));
    }

    if (color_source)
    {
        if (const osg::Vec4Array* source = sources[color_source].getVec4Array())
        {
            osg::Vec4Array* pArray = new osg::Vec4Array;

            for (VertexIndicesIndexMap::const_iterator it = vertexIndicesIndexMap.begin(),
                end = vertexIndicesIndexMap.end(); it != end; ++it)
            {
                pArray->push_back(source->at(it->first.color_index));
            }

            geometry->setColorData(osg::Geometry::ArrayData(pArray, osg::Geometry::BIND_PER_VERTEX));
        }
    }

    if (normal_source)
    {
        if (const osg::Vec3Array* source = sources[normal_source].getVec3Array())
        {
            osg::Vec3Array* pArray = new osg::Vec3Array;

            for (VertexIndicesIndexMap::const_iterator it = vertexIndicesIndexMap.begin(),
                end = vertexIndicesIndexMap.end(); it != end; ++it)
            {
                pArray->push_back(source->at(it->first.normal_index));
            }

            geometry->setNormalData(osg::Geometry::ArrayData(pArray, osg::Geometry::BIND_PER_VERTEX));
        }
    }

    for (int texcoord_set = 0; texcoord_set < MAX_TEXTURE_COORDINATE_SETS; ++texcoord_set)
    {
        if (daeElement* texcoord_source = texcoord_sources[texcoord_set])
        {
            osg::Array* pArray = NULL;

            if (const osg::Vec2Array* source = sources[texcoord_source].getVec2Array())
            {
                osg::Vec2Array* pVec2Array = new osg::Vec2Array;
                pArray = pVec2Array;

                for (VertexIndicesIndexMap::const_iterator it = vertexIndicesIndexMap.begin(),
                    end = vertexIndicesIndexMap.end(); it != end; ++it)
                {
                    pVec2Array->push_back(source->at(it->first.texcoord_indices[texcoord_set]));
                }
            }
            else if (const osg::Vec3Array* source = sources[texcoord_source].getVec3Array())
            {
                osg::Vec3Array* pVec3Array = new osg::Vec3Array;
                pArray = pVec3Array;

                for (VertexIndicesIndexMap::const_iterator it = vertexIndicesIndexMap.begin(),
                    end = vertexIndicesIndexMap.end(); it != end; ++it)
                {
                    pVec3Array->push_back(source->at(it->first.texcoord_indices[texcoord_set]));
                }
            }

            if (pArray)
            {
                geometry->setTexCoordData(texcoord_set, osg::Geometry::ArrayData(pArray, osg::Geometry::BIND_PER_VERTEX));
            }
        }
    }
}
