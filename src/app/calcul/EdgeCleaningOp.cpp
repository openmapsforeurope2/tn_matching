// APP
#include <app/calcul/EdgeCleaningOp.h>
#include <app/params/ThemeParameters.h>

// BOOST
#include <boost/progress.hpp>

// EPG
#include <epg/Context.h>
#include <epg/params/EpgParameters.h>
#include <epg/sql/tools/numFeatures.h>
#include <epg/sql/DataBaseManager.h>
#include <epg/tools/StringTools.h>
#include <epg/tools/TimeTools.h>
#include <epg/tools/geometry/isSlimSurface.h>

// SOCLE
#include <ign/geometry/graph/detail/NextEdge.h>

namespace app
{
    namespace calcul
    {

        ///
        ///
        ///
        void EdgeCleaningOp::clean(
            std::string edgeTable,
            std::string borderCode,
            bool verbose)
        {
            EdgeCleaningOp op(edgeTable, borderCode, verbose);
            op._clean();
        }

        ///
        ///
        ///
        EdgeCleaningOp::EdgeCleaningOp(
            std::string edgeTable,
            std::string borderCode,
            bool verbose
        ) : _verbose(verbose)
        {
            _init(edgeTable, borderCode);
        }

        ///
        ///
        ///
        EdgeCleaningOp::~EdgeCleaningOp()
        {
            _shapeLogger->closeShape("edge_cleaning_deleted_edges");
            _shapeLogger->closeShape("edge_cleaning_country");
        }

        ///
        ///
        ///
        void EdgeCleaningOp::_init(std::string edgeTable, std::string borderCode)
        {
            //--
            _logger = epg::log::EpgLoggerS::getInstance();
            _logger->log(epg::log::INFO, "[START] initialization: " + epg::tools::TimeTools::getTime());

            //--
            _shapeLogger = epg::log::ShapeLoggerS::getInstance();
            _shapeLogger->addShape("edge_cleaning_deleted_edges", epg::log::ShapeLogger::LINESTRING);
            _shapeLogger->addShape("edge_cleaning_country", epg::log::ShapeLogger::POLYGON);

            //--
            epg::Context *context = epg::ContextS::getInstance();

            // epg parameters
            epg::params::EpgParameters const &epgParams = context->getEpgParameters();

            std::string const boundaryTableName = epgParams.getValue(TARGET_BOUNDARY_TABLE).toString();
            std::string const idName = epgParams.getValue(ID).toString();
            std::string const geomName = epgParams.getValue(GEOM).toString();
            std::string const countryCodeName = epgParams.getValue(COUNTRY_CODE).toString();
            
            // app parameters
            params::ThemeParameters *themeParameters = params::ThemeParametersS::getInstance();
            std::string const landmaskTableName = themeParameters->getValue(LANDMASK_TABLE).toString();
            std::string const landCoverTypeName = themeParameters->getValue(LAND_COVER_TYPE).toString();
            std::string const landAreaValue = themeParameters->getValue(TYPE_LAND_AREA).toString();
            std::string const clTableName = themeParameters->getValue(CL_TABLE).toString();


            // on recupere un buffer autour de la frontiere
            ign::geometry::GeometryPtr boundBuffPtr(new ign::geometry::Polygon());
            ign::feature::sql::FeatureStorePostgis* fsBoundary = context->getDataBaseManager().getFeatureStore(boundaryTableName, idName, geomName);
            ign::feature::FeatureIteratorPtr itBoundary = fsBoundary->getFeatures(ign::feature::FeatureFilter(countryCodeName +" = '"+borderCode+"'"));
            while (itBoundary->hasNext())
            {
                ign::feature::Feature const& fBoundary = itBoundary->next();
                ign::geometry::LineString const& ls = fBoundary.getGeometry().asLineString();

                ign::geometry::GeometryPtr tmpBuffPtr(ls.buffer(3000));

                boundBuffPtr.reset(boundBuffPtr->Union(*tmpBuffPtr));
            }

            //on recupere la geometry des pays
            std::vector<std::string> vCountry;
		    epg::tools::StringTools::Split(borderCode, "#", vCountry);

            for (std::vector<std::string>::iterator vit = vCountry.begin() ; vit != vCountry.end() ; ++vit) {
                ign::geometry::MultiPolygon mpLandmask;
                ign::feature::sql::FeatureStorePostgis* fsLandmask = context->getDataBaseManager().getFeatureStore(landmaskTableName, idName, geomName);
                ign::feature::FeatureIteratorPtr itLandmask = fsLandmask->getFeatures(ign::feature::FeatureFilter(landCoverTypeName + " = '" + landAreaValue + "' AND " + countryCodeName + " = '" + *vit + "'"));
                while (itLandmask->hasNext())
                {
                    ign::feature::Feature const& fLandmask = itLandmask->next();
                    ign::geometry::MultiPolygon const& mp = fLandmask.getGeometry().asMultiPolygon();
                    for (int i = 0; i < mp.numGeometries(); ++i)
                    {
                        mpLandmask.addGeometry(mp.polygonN(i));
                    }
                }

                //on calcul la geometry de travail
                _mCountryGeomPtr.insert(std::make_pair(*vit, ign::geometry::GeometryPtr(boundBuffPtr->Intersection(mpLandmask)) ));

                ign::feature::Feature feat;
                feat.setGeometry(*_mCountryGeomPtr[*vit]);
                _shapeLogger->writeFeature("edge_cleaning_country", feat);
            }

            //--
            _fsEdge = context->getDataBaseManager().getFeatureStore(edgeTable, idName, geomName);

            //--
            _fsCl = context->getDataBaseManager().getFeatureStore(clTableName, idName, geomName);

            //--
            _logger->log(epg::log::INFO, "[END] initialization: " + epg::tools::TimeTools::getTime());
        };

        ///
        ///
        ///
        void EdgeCleaningOp::_loadGraph(app::calcul::detail::EdgeCleaningGraphManager & graphManager) const
        {
            epg::Context *context = epg::ContextS::getInstance();
            epg::params::EpgParameters const& epgParams = context->getEpgParameters();
            std::string const countryCodeName = epgParams.getValue(COUNTRY_CODE).toString();

            // chargement des edges
            // patience
            int numFeatures = epg::sql::tools::numFeatures(*_fsEdge, ign::feature::FeatureFilter());
            boost::progress_display display(numFeatures, std::cout, "[ edge_loading  % complete ]\n");

            ign::feature::FeatureIteratorPtr itEdge = _fsEdge->getFeatures(ign::feature::FeatureFilter());
            while (itEdge->hasNext())
            {
                ++display;
                ign::feature::Feature const& fEdge = itEdge->next();
                ign::geometry::LineString const& ls = fEdge.getGeometry().asLineString();
                std::string edgeId = fEdge.getId();
                std::string country = fEdge.getAttribute(countryCodeName).toString();

                graphManager.addEdge(ls, edgeId, EdgeCleaningEdge(country));
            }

            // chargement des CL
            // patience
            int numCl = epg::sql::tools::numFeatures(*_fsCl, ign::feature::FeatureFilter());
            boost::progress_display displayCl(numCl, std::cout, "[ cl_loading  % complete ]\n");

            ign::feature::FeatureIteratorPtr itCl = _fsCl->getFeatures(ign::feature::FeatureFilter());
            while (itCl->hasNext())
            {
                ++displayCl;
                ign::feature::Feature const& fCl = itCl->next();
                ign::geometry::LineString const& ls = fCl.getGeometry().asLineString();
                std::string clId = fCl.getId();
                std::string country = fCl.getAttribute(countryCodeName).toString();

                graphManager.addEdge(ls, clId, EdgeCleaningEdge(country, true));
            }

            graphManager.planarize();
            graphManager.createFaces();
        }

        ///
        double EdgeCleaningOp::_getLength( ign::geometry::Geometry const& geom ) const
        {
            ign::geometry::Geometry::GeometryType geomType = geom.getGeometryType();
            switch( geomType )
            {
                case ign::geometry::Geometry::GeometryTypeNull :
                case ign::geometry::Geometry::GeometryTypePoint :
                case ign::geometry::Geometry::GeometryTypeMultiPoint :
                case ign::geometry::Geometry::GeometryTypeTriangle :
                case ign::geometry::Geometry::GeometryTypeTriangulatedSurface :
                case ign::geometry::Geometry::GeometryTypePolyhedralSurface :
                case ign::geometry::Geometry::GeometryTypePolygon :
                case ign::geometry::Geometry::GeometryTypeMultiPolygon :
                    return 0;
                case ign::geometry::Geometry::GeometryTypeLineString :
                    {
                        ign::geometry::LineString const& ls = geom.asLineString();
                        if (ls.isEmpty()) return 0;
                        double length = ls.length();
                        return length;
                    }
                    
                case ign::geometry::Geometry::GeometryTypeMultiLineString : 
                    {
                        double length = 0;
                        ign::geometry::MultiLineString const& mls = geom.asMultiLineString();
                        for( size_t i = 0 ; i < mls.numGeometries() ; ++i )
                            length += mls.lineStringN(i).length();
                        return length;
                    }
                
                case ign::geometry::Geometry::GeometryTypeGeometryCollection :
                    {
                        double length = 0;
                        ign::geometry::GeometryCollection const& collection = geom.asGeometryCollection();
                        for( size_t i = 0 ; i < collection.numGeometries() ; ++i )
                            length += _getLength( collection.geometryN(i) );
                    
                        return length;
                    }
                default :
                    IGN_THROW_EXCEPTION( "Geometry type not allowed" );
            }
        }

        ///
        ///
        ///
        void EdgeCleaningOp::_addLengths(std::string country, ign::geometry::LineString const& ls , double & lengthInCountry, double & length) const 
        {
            std::map<std::string, ign::geometry::GeometryPtr>::const_iterator mit = _mCountryGeomPtr.find(country);
            if (mit == _mCountryGeomPtr.end()) {
                _logger->log(epg::log::ERROR, "Unknown country [country code] " + country);
                return;
            }

            ign::geometry::GeometryPtr resultPtr(mit->second->Intersection(ls));

            double ratio = 0;

            lengthInCountry += _getLength(*resultPtr);
            length += ls.length();
        }

        ///
        ///
        ///
        double EdgeCleaningOp::_getRatio(GraphType const& graph, std::string country, std::list<edge_descriptor> const& lEdges) const
        {
            double lengthInCountry = 0;
            double length = 0;
            std::list<edge_descriptor>::const_iterator lit = lEdges.begin();
            for ( ; lit != lEdges.end() ; ++lit) {
                ign::geometry::LineString edgeGeom = graph.getGeometry(*lit);
                _addLengths(country, edgeGeom, lengthInCountry, length);
            }
            return lengthInCountry / length;
        }

        ///
        ///
        ///
        std::string EdgeCleaningOp::_toString(std::vector<std::string> const& vStrings, std::string separator) const {
            std::string result = "";
            std::vector<std::string>::const_iterator vit;
            for ( vit = vStrings.begin() ; vit != vStrings.end() ; ++vit ) {
                if ( !result.empty() ) result += separator;
                result += *vit;
            }
            return result;
        }

        ///
        ///
        ///
        void EdgeCleaningOp::_removeEdges(GraphType const& graph, std::list<edge_descriptor> const& lEdges) const
        {
            std::list<edge_descriptor>::const_iterator lit = lEdges.begin();
            for ( ; lit != lEdges.end() ; ++lit) {
                if (graph.origins(*lit).size() > 1) {
                    _logger->log(epg::log::WARN, "Edge with multiple origins [cl id] "+_toString(graph.origins(*lit)));
                }
                for (size_t i = 0 ; i < graph.origins(*lit).size() ; ++i) {
                     std::string edgeId = graph.origins(*lit)[0];

                    ign::feature::Feature dFeat;
                    _fsEdge->getFeatureById(edgeId, dFeat);
                    _shapeLogger->writeFeature("edge_cleaning_deleted_edges", dFeat);

                    // _fsEdge->deleteFeature(edgeId);
                }
            }
        }

        ///
        ///
        ///
        void EdgeCleaningOp::_clean() const
        {
            epg::Context *context = epg::ContextS::getInstance();

            // epg parameters
            epg::params::EpgParameters const &epgParams = context->getEpgParameters();
            std::string const countryCodeName = epgParams.getValue(COUNTRY_CODE).toString();

            // app parameters
            params::ThemeParameters* themeParameters = params::ThemeParametersS::getInstance();
            double const slimSurfaceWidth = themeParameters->getValue( SLIM_SURFACE_WIDTH ).toDouble();
            double const antennaRatioThreshold = themeParameters->getValue( ANTENNA_RATIO_THRESHOLD ).toDouble();

            detail::EdgeCleaningGraphManager graphManager;
            _loadGraph(graphManager);

            GraphType const& graph = graphManager.getGraph();
            boost::progress_display display(graph.numFaces(), std::cout, "[ cleaning faces  % complete ]\n");
            face_iterator fit, fend;
			for( graph.faces( fit, fend ) ; fit != fend ; ++fit )
			{
                ++display;
				ign::geometry::Polygon faceGeom = graph.getGeometry( *fit );
				if (epg::tools::geometry::isSlimSurface(faceGeom, slimSurfaceWidth)) {

                    std::map<std::string, std::list<edge_descriptor>> mlEdges;

                    oriented_edge_descriptor startEdge = graph.getIncidentEdge( *fit );
                    oriented_edge_descriptor nextEdge = startEdge;
                    std::string previousCountry = graphManager.getCountry(nextEdge.descriptor);
                    bool endingPointPassed = false;
                    bool aborded = false;
                    do{
                        // on ne doit pas avoir de cl dans la boucle
                        if (graphManager.isCl(nextEdge.descriptor)) {
                            _logger->log(epg::log::WARN, "Loop contains a CL [cl id] "+graph.origins(nextEdge.descriptor)[0]);
                            aborded = true;
                            break;
                        }

                        std::string country = graphManager.getCountry(nextEdge.descriptor);

                        if (endingPointPassed) {
                            if (previousCountry == country) {
                                _logger->log(epg::log::WARN, "Same country on incident edges [edge id] "+graph.origins(nextEdge.descriptor)[0]);
                                aborded = true;
                                break;
                            }
                            endingPointPassed == false;
                        } else if ( previousCountry != country ) {
                            _logger->log(epg::log::WARN, "Mixed country on path [edge id] "+graph.origins(nextEdge.descriptor)[0]);
                            aborded = true;
                            break;
                        }

                        std::map<std::string, std::list<edge_descriptor>>::iterator mlit = mlEdges.find(country);
                        if (mlit == mlEdges.end()) mlit = mlEdges.insert(std::make_pair(country, std::list<edge_descriptor>())).first;
                        mlit->second.push_back(nextEdge.descriptor);

                        if (graph.degree(graph.target(nextEdge)) > 2) {
                            std::vector< edge_descriptor > vIncidents = graph.incidentEdges(graph.target(nextEdge));
                            for (std::vector<edge_descriptor>::const_iterator vit = vIncidents.begin() ; vit != vIncidents.end() ; ++vit) {
                                if(graphManager.isCl(*vit)) {
                                    endingPointPassed = true;
                                    break;
                                }
                            }
                        }
                        nextEdge = ign::geometry::graph::detail::nextEdge( nextEdge, graph );
                    }while( nextEdge != startEdge );

                    if (aborded) continue;

                    if ( mlEdges.size() > 2 ) {
                        _logger->log(epg::log::WARN, "More than 2 paths found path [edge id] "+graph.origins(startEdge.descriptor)[0]);
                        continue;
                    }

                    if ( mlEdges.size() == 1 ) {
                        _logger->log(epg::log::WARN, "Only 1 path found path [edge id] "+graph.origins(startEdge.descriptor)[0]);
                        continue;
                    }

                    if ( mlEdges.size() == 0 ) {
                        _logger->log(epg::log::WARN, "No path found path [edge id] "+graph.origins(startEdge.descriptor)[0]);
                        continue;
                    }

                    // quel chemin doit-on garder ?
                    double ratio1 = _getRatio(graph, mlEdges.begin()->first, mlEdges.begin()->second);
                    double ratio2 = _getRatio(graph, mlEdges.rbegin()->first, mlEdges.rbegin()->second);

                    if ( ratio1 > ratio2 ) {
                        _removeEdges(graph, mlEdges.rbegin()->second);
                    } else {
                        _removeEdges(graph, mlEdges.begin()->second);
                    }
                }
			}

            // nettoyage des antennes
            std::vector<std::pair<std::string, std::list<edge_descriptor>>> vpAntennas;
            std::set< vertex_descriptor > visitedVertices;

            boost::progress_display display2(graph.numVertices(), std::cout, "[ cleaning antennas  % complete ]\n");
            vertex_iterator vit, vend;
            for( graph.vertices( vit, vend ) ; vit != vend ; ++vit )
            {
                ++display2;
                if( visitedVertices.find( *vit ) != visitedVertices.end() ) continue;
                if( graph.degree( *vit ) != 1 ) continue;

                // seulement les vertex qui touchent une CL ?

                std::list<edge_descriptor> lAntennaEdges;

                std::vector< oriented_edge_descriptor > vEdges;
                graph.incidentEdges( *vit, vEdges );

                oriented_edge_descriptor nextEdge = vEdges.front(); // si nextEdge n'est pas une CL ?
                if (graphManager.isCl(nextEdge.descriptor)) {
                    _logger->log(epg::log::WARN, "Antenna is connecting line [cl id] "+graph.origins(nextEdge.descriptor)[0]);
                    continue;
                }
                std::string country = graphManager.getCountry(nextEdge.descriptor);

                //DEBUG
                // if (country=="be#fr") {
                //     bool test = true;
                // }
                // std::string id = graph.origins(nextEdge.descriptor)[0];
                // if (id == "a15662a7-be3c-434f-8124-2a3ab80691d8") {
                //     bool test = true;
                // }

                vertex_descriptor vTarget = GraphType::nullVertex();
                
                double length = 0.;
                while( true )
                {
                    lAntennaEdges.push_back(nextEdge.descriptor);

                    vTarget = graph.target( nextEdge );

                    if (graphManager.isCl(nextEdge.descriptor)) {
                        _logger->log(epg::log::WARN, "Antenna connected to connecting line [cl id] "+graph.origins(nextEdge.descriptor)[0]);
                        break;
                    }

                    if( graph.degree( vTarget ) != 2 ) { // ou si nextEdge est une CL ?
                        if( graph.degree( vTarget ) == 1 /*antenne isolee*/)
                        {
                            visitedVertices.insert( vTarget );
                        }
                        break;
                    }

                    std::vector< oriented_edge_descriptor > vIncEdges;
                    graph.incidentEdges( vTarget, vIncEdges );

                    nextEdge = ( vIncEdges.front().descriptor == nextEdge.descriptor )? vIncEdges.back():vIncEdges.front();
                }

                if (!lAntennaEdges.empty()) vpAntennas.push_back(std::make_pair(country, lAntennaEdges));
            }

            std::vector<std::pair<std::string, std::list<edge_descriptor>>>::const_iterator vpit;
            for (vpit = vpAntennas.begin() ; vpit != vpAntennas.end() ; ++vpit) {
                double ratio = _getRatio(graph, vpit->first, vpit->second);
                if (ratio < antennaRatioThreshold) {
                    _removeEdges(graph, vpit->second);
                }
            }
        };
    }
}