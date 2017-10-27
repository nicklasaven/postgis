
/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PostGIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * PostGIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostGIS.  If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************
 *
 * Copyright 2017 Nicklas Avén
 *
 **********************************************************************/


#include "liblwgeom_internal.h"

static POINTARRAY* ptarray_filterm(POINTARRAY *pa,double min, double max)
{
 

    /*Check if M exists
     * This should already be tested earlier so we don't handle it properly.
     * If this happens it is because the function is used in another context than filterM
     and we throw an error*/
    if(!FLAGS_GET_M(pa->flags))
        lwerror("missing m-value in function %s\n",__func__);
        
    int ndims = FLAGS_NDIMS(pa->flags);
    
    int pointsize = ndims * sizeof(double);
    double m_val;
    
    //M-value willl always be the last dimension
    int m_pos = ndims-1;
    
    int i, counter=0;
    for(i=0;i<pa->npoints;i++)
    {
        m_val = *((double*)pa->serialized_pointlist + i*ndims + m_pos);
        if(m_val > min && m_val <= max)
            counter++;
    }
    
    POINTARRAY *pa_res = ptarray_construct(FLAGS_GET_Z(pa->flags), FLAGS_GET_M(pa->flags), counter);
    
    /*Trying to reduce number of memcpy operations
     * Don't know if it is worth it.
     * But when full pa is requested (no points filtered away
     * this will be just 1 memcpy */
    int add_n = 0;
    double *add_p = (double*) pa->serialized_pointlist;
    double *res_cursor = (double*) pa_res->serialized_pointlist;
    for(i=0;i<pa->npoints;i++)
    {
        m_val = *((double*)pa->serialized_pointlist + i*ndims + m_pos);
        if(m_val > min && m_val <= max)
        {
            if(!add_n)
                add_p = (double*) pa->serialized_pointlist + i*ndims;
            add_n++;            
        }
        else
        {
            if(add_n)
            {
                memcpy(res_cursor, add_p, add_n*pointsize);
                res_cursor+=add_n*ndims;
                add_n=0;               
            }
        }
    }
    if(add_n)
    {
        memcpy(res_cursor, add_p, add_n*pointsize);
    }
    
    return pa_res;
    
}
static LWPOINT* lwpoint_filterm(LWPOINT  *pt,double min,double max)
{
    LWDEBUGF(2, "Entered %s", __func__);

    POINTARRAY *pa;

	pa = ptarray_filterm(pt->point, min, max);
    
	return lwpoint_construct(SRID_UNKNOWN, NULL, pa);
    
}

static LWLINE* lwline_filterm(LWLINE  *line,double min,double max)
{
    
    LWDEBUGF(2, "Entered %s", __func__);
    
    
	POINTARRAY *pa;

	pa = ptarray_filterm(line->points, min, max);


	if(pa->npoints < 2 )
	{
		lwerror("Linestring must have at least two points");
		return NULL;
	}

	return lwline_construct(SRID_UNKNOWN, NULL, pa);   
}

static LWPOLY* lwpoly_filterm(LWPOLY  *poly,double min,double max)
{
	int i, nrings;
    LWPOLY *poly_res = lwpoly_construct_empty(SRID_UNKNOWN, FLAGS_GET_Z(poly->flags), FLAGS_GET_M(poly->flags));
    
    nrings = poly->nrings;
	for( i = 0; i < nrings; i++ )
	{
		/* Ret number of points */
		POINTARRAY *pa = ptarray_filterm(poly->rings[i], min, max);
       
		/* Skip empty rings */
		if( pa == NULL )
			continue;


        if(pa->npoints>=4)
		{
			if (lwpoly_add_ring(poly_res, pa) == LW_FAILURE )
            {
                LWDEBUG(2, "Unable to add ring to polygon");
                lwerror("Unable to add ring to polygon");
            }
		}
		else if (i==0)/*Inner rings we allow to ocollapse so we only check outer ring*/
        {
			LWDEBUG(2, "Polygons must have at least four points in each ring");
			lwerror("Polygons must have at least four points in each ring");
			return NULL;
		}
    }
	return poly_res;
}


LWGEOM* filter_m(LWGEOM *geom, double min,double max)
{    
    LWGEOM *geom_out = NULL;
    
    if(!FLAGS_GET_M(geom->flags))
        return geom;
    //lwnotice("geom type = %d\n",geom->type);
    switch ( geom->type )
	{
		case POINTTYPE:
		{
			LWDEBUGF(4,"Type found is Point, %d", geom->type);
			geom_out = lwpoint_as_lwgeom(lwpoint_filterm((LWPOINT*) geom, min, max));
            break;
		}
		case LINETYPE:
		{
			LWDEBUGF(4,"Type found is Linestring, %d", geom->type);
			geom_out = lwline_as_lwgeom(lwline_filterm((LWLINE*) geom, min, max));
            break;
		}
		/* Polygon has 'nrings' and 'rings' elements */
		case POLYGONTYPE:
		{
			LWDEBUGF(4,"Type found is Polygon, %d", geom->type);
			geom_out = lwpoly_as_lwgeom(lwpoly_filterm((LWPOLY*)geom,min, max));
            break;
		}

		/* All these Collection types have 'ngeoms' and 'geoms' elements */
		case MULTIPOINTTYPE:
		case MULTILINETYPE:
		case MULTIPOLYGONTYPE:
		{
			LWDEBUGF(4,"Type found is Multi, %d", geom->type);
			//geom_out = lwmulti_filterm((LWCOLLECTION*)geom, globals, ts);
		}
		case COLLECTIONTYPE:
		{
			LWDEBUGF(4,"Type found is collection, %d", geom->type);
		//	geom_out = lwcollection_filterm((LWCOLLECTION*) geom, globals, ts);
		}
		/* Unknown type! */
		default:
			lwerror("Unsupported geometry type: %s [%d] in function %s", lwtype_name((geom)->type), (geom)->type, __func__);
	}

	return geom_out;
    
    
}
