/******************************************************************************
 * $Id: ogrmemlayer.cpp 17807 2009-10-13 18:18:09Z rouault $
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Implements OGRMemLayer class.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2003, Frank Warmerdam <warmerdam@pobox.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "ogr_mem.h"
#include "cpl_conv.h"

CPL_CVSID("$Id: ogrmemlayer.cpp 17807 2009-10-13 18:18:09Z rouault $");

/************************************************************************/
/*                            OGRMemLayer()                             */
/************************************************************************/

OGRMemLayer::OGRMemLayer( const char * pszName, OGRSpatialReference *poSRSIn, 
                          OGRwkbGeometryType eReqType )

{
    if( poSRSIn == NULL )
        poSRS = NULL;
    else
        poSRS = poSRSIn->Clone();
    
    iNextReadFID = 0;
    iNextCreateFID = 0;

    nFeatureCount = 0;
    nMaxFeatureCount = 0;
    papoFeatures = NULL;

    poFeatureDefn = new OGRFeatureDefn( pszName );
    poFeatureDefn->SetGeomType( eReqType );
    poFeatureDefn->Reference();
}

/************************************************************************/
/*                           ~OGRMemLayer()                           */
/************************************************************************/

OGRMemLayer::~OGRMemLayer()

{
    if( m_nFeaturesRead > 0 && poFeatureDefn != NULL )
    {
        CPLDebug( "Mem", "%d features read on layer '%s'.",
                  (int) m_nFeaturesRead, 
                  poFeatureDefn->GetName() );
    }

    for( int i = 0; i < nMaxFeatureCount; i++ )
    {
        if( papoFeatures[i] != NULL )
            delete papoFeatures[i];
    }
    CPLFree( papoFeatures );

    if( poFeatureDefn )
        poFeatureDefn->Release();

    if( poSRS )
        poSRS->Release();
}

/************************************************************************/
/*                            ResetReading()                            */
/************************************************************************/

void OGRMemLayer::ResetReading()

{
    iNextReadFID = 0;
}

/************************************************************************/
/*                           GetNextFeature()                           */
/************************************************************************/

OGRFeature *OGRMemLayer::GetNextFeature()

{
    while( iNextReadFID < nMaxFeatureCount )
    {
        OGRFeature *poFeature = papoFeatures[iNextReadFID++];

        if( poFeature == NULL )
            continue;

        if( (m_poFilterGeom == NULL
             || FilterGeometry( poFeature->GetGeometryRef() ) )
            && (m_poAttrQuery == NULL
                || m_poAttrQuery->Evaluate( poFeature ) ) )
        {
            m_nFeaturesRead++;
            return poFeature->Clone();
        }
    }

    return NULL;
}

/************************************************************************/
/*                           SetNextByIndex()                           */
/************************************************************************/

OGRErr OGRMemLayer::SetNextByIndex( long nIndex )

{
    if( m_poFilterGeom != NULL || m_poAttrQuery != NULL )
        return OGRLayer::SetNextByIndex( nIndex );
        
    if (iNextReadFID < 0 || iNextReadFID >= nMaxFeatureCount)
        return OGRERR_FAILURE;

    iNextReadFID = nIndex;

    return OGRERR_NONE;
}

/************************************************************************/
/*                             GetFeature()                             */
/************************************************************************/

OGRFeature *OGRMemLayer::GetFeature( long nFeatureId )

{
    if( nFeatureId < 0 || nFeatureId >= nMaxFeatureCount )
        return NULL;
    else if( papoFeatures[nFeatureId] == NULL )
        return NULL;
    else
        return papoFeatures[nFeatureId]->Clone();
}

/************************************************************************/
/*                             SetFeature()                             */
/************************************************************************/

OGRErr OGRMemLayer::SetFeature( OGRFeature *poFeature )

{
    if( poFeature == NULL )
        return OGRERR_FAILURE;

    if( poFeature->GetFID() == OGRNullFID )
    {
        while( iNextCreateFID < nMaxFeatureCount 
               && papoFeatures[iNextCreateFID] != NULL )
            iNextCreateFID++;
        poFeature->SetFID( iNextCreateFID++ );
    }
    else if ( poFeature->GetFID() < OGRNullFID )
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "negative FID are not supported");
        return OGRERR_FAILURE;
    }

    if( poFeature->GetFID() >= nMaxFeatureCount )
    {
        int nNewCount = MAX(2*nMaxFeatureCount+10, poFeature->GetFID() + 1 );

        OGRFeature** papoNewFeatures = (OGRFeature **) 
            VSIRealloc( papoFeatures, sizeof(OGRFeature *) * nNewCount);
        if (papoNewFeatures == NULL)
        {
            CPLError(CE_Failure, CPLE_OutOfMemory,
                     "Cannot allocate array of %d elements", nNewCount);
            return OGRERR_FAILURE;
        }
        papoFeatures = papoNewFeatures;
        memset( papoFeatures + nMaxFeatureCount, 0, 
                sizeof(OGRFeature *) * (nNewCount - nMaxFeatureCount) );
        nMaxFeatureCount = nNewCount;
    }

    if( papoFeatures[poFeature->GetFID()] != NULL )
    {
        delete papoFeatures[poFeature->GetFID()];
        papoFeatures[poFeature->GetFID()] = NULL;
        nFeatureCount--;
    }

    papoFeatures[poFeature->GetFID()] = poFeature->Clone();
    nFeatureCount++;

    return OGRERR_NONE;
}

/************************************************************************/
/*                           CreateFeature()                            */
/************************************************************************/

OGRErr OGRMemLayer::CreateFeature( OGRFeature *poFeature )

{
    if( poFeature->GetFID() != OGRNullFID 
        && poFeature->GetFID() >= 0
        && poFeature->GetFID() < nMaxFeatureCount )
    {
        if( papoFeatures[poFeature->GetFID()] != NULL )
            poFeature->SetFID( OGRNullFID );
    }

    if( poFeature->GetFID() > 10000000 )
        poFeature->SetFID( OGRNullFID );

    return SetFeature( poFeature );
}

/************************************************************************/
/*                           DeleteFeature()                            */
/************************************************************************/

OGRErr OGRMemLayer::DeleteFeature( long nFID )

{
    if( nFID < 0 || nFID >= nMaxFeatureCount 
        || papoFeatures[nFID] == NULL )
    {
        return OGRERR_FAILURE;
    }
    else 
    {
        delete papoFeatures[nFID];
        papoFeatures[nFID] = NULL;
        nFeatureCount--;
        return OGRERR_NONE;
    }
}

/************************************************************************/
/*                          GetFeatureCount()                           */
/*                                                                      */
/*      If a spatial filter is in effect, we turn control over to       */
/*      the generic counter.  Otherwise we return the total count.      */
/*      Eventually we should consider implementing a more efficient     */
/*      way of counting features matching a spatial query.              */
/************************************************************************/

int OGRMemLayer::GetFeatureCount( int bForce )

{
    if( m_poFilterGeom != NULL || m_poAttrQuery != NULL )
        return OGRLayer::GetFeatureCount( bForce );
    else
        return nFeatureCount;
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRMemLayer::TestCapability( const char * pszCap )

{
    if( EQUAL(pszCap,OLCRandomRead) )
        return TRUE;

    else if( EQUAL(pszCap,OLCSequentialWrite) 
             || EQUAL(pszCap,OLCRandomWrite) )
        return TRUE;

    else if( EQUAL(pszCap,OLCFastFeatureCount) )
        return m_poFilterGeom == NULL && m_poAttrQuery == NULL;

    else if( EQUAL(pszCap,OLCFastSpatialFilter) )
        return FALSE;

    else if( EQUAL(pszCap,OLCDeleteFeature) )
        return TRUE;

    else if( EQUAL(pszCap,OLCCreateField) )
        return TRUE;

    else if( EQUAL(pszCap,OLCFastSetNextByIndex) )
        return m_poFilterGeom == NULL && m_poAttrQuery == NULL;

    else 
        return FALSE;
}

/************************************************************************/
/*                            CreateField()                             */
/************************************************************************/

OGRErr OGRMemLayer::CreateField( OGRFieldDefn *poField, int bApproxOK )

{
/* -------------------------------------------------------------------- */
/*      simple case, no features exist yet.                             */
/* -------------------------------------------------------------------- */
    if( nFeatureCount == 0 )
    {
        poFeatureDefn->AddFieldDefn( poField );
        return OGRERR_NONE;
    }

/* -------------------------------------------------------------------- */
/*      Add field definition and setup remap definition.                */
/* -------------------------------------------------------------------- */
    int  *panRemap;
    int   i;

    poFeatureDefn->AddFieldDefn( poField );

    panRemap = (int *) CPLMalloc(sizeof(int) * poFeatureDefn->GetFieldCount());
    for( i = 0; i < poFeatureDefn->GetFieldCount(); i++ )
    {
        if( i < poFeatureDefn->GetFieldCount() - 1 )
            panRemap[i] = i;
        else
            panRemap[i] = -1;
    }

/* -------------------------------------------------------------------- */
/*      Remap all the internal features.  Hopefully there aren't any    */
/*      external features referring to our OGRFeatureDefn!              */
/* -------------------------------------------------------------------- */
    for( i = 0; i < nMaxFeatureCount; i++ )
    {
        if( papoFeatures[i] != NULL )
            papoFeatures[i]->RemapFields( NULL, panRemap );
    }

    CPLFree( panRemap );

    return OGRERR_NONE;
}

/************************************************************************/
/*                           GetSpatialRef()                            */
/************************************************************************/

OGRSpatialReference *OGRMemLayer::GetSpatialRef()

{
    return poSRS;
}
