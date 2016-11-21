/*

  osmium_waterareas
  -----------------------------------------------------
  extracts all water area polygons from an OSM file
  and writes them into spatialite database

  by Christoph Hormann <chris_hormann@gmx.de>
  based on osmium_toogr2 example

*/

#include <iostream>
#include <getopt.h>

#include <gdalcpp.hpp>

#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/index/map/dense_mem_array.hpp>
//#include <osmium/index/map/dense_file_array.hpp>
#include <osmium/index/map/dense_mmap_array.hpp>
#include <osmium/index/map/dummy.hpp>

#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/visitor.hpp>
#include <osmium/area/multipolygon_collector.hpp>
#include <osmium/area/assembler.hpp>

#include <osmium/geom/mercator_projection.hpp>
//#include <osmium/geom/projection.hpp>
#include <osmium/geom/ogr.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/handler/dump.hpp>
#include <osmium/util/memory.hpp>
#include <osmium/util/verbose_output.hpp>
#include <osmium/visitor.hpp>

#include <osmium/index/node_locations_map.hpp>

//using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
//using index_type = osmium::index::map::DenseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
//using index_type = osmium::index::map::DenseFileArray<osmium::unsigned_object_id_type, osmium::Location>;

using index_type = osmium::index::map::Map<osmium::unsigned_object_id_type, osmium::Location>;

using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

REGISTER_MAP(osmium::unsigned_object_id_type, osmium::Location, osmium::index::map::Dummy, none)

template <class TProjection>
class MyOGRHandler : public osmium::handler::Handler {

    gdalcpp::Layer m_layer_polygon;
    osmium::geom::OGRFactory<TProjection>& m_factory;

public:

    MyOGRHandler(gdalcpp::Dataset& dataset, osmium::geom::OGRFactory<TProjection>& factory) :
        m_layer_polygon(dataset, "waterareas", wkbMultiPolygon),
        m_factory(factory) {

        m_layer_polygon.add_field("id", OFTReal, 10);
        m_layer_polygon.add_field("type", OFTString, 30);
        m_layer_polygon.add_field("ftype", OFTInteger, 2);
        m_layer_polygon.add_field("intermittent", OFTInteger, 2);
        m_layer_polygon.add_field("salt", OFTInteger, 2);
        m_layer_polygon.add_field("maritime", OFTInteger, 2);
        m_layer_polygon.add_field("name", OFTString, 64);
    }

    void area(const osmium::Area& area) {

			const char* water_tag = area.tags()["water"];
			const char* natural_tag = area.tags()["natural"];
			const char* waterway_tag = area.tags()["waterway"];
			const char* landuse_tag = area.tags()["landuse"];

			const char* Type = 0;

			// water=* has priority over waterway=riverbank and landuse=reservoir
			if (water_tag && !strcmp(water_tag, "reservoir") && 
					natural_tag && !strcmp(natural_tag, "water")) Type = "reservoir";
			else if (water_tag && !strcmp(water_tag, "river") && 
							 natural_tag && !strcmp(natural_tag, "water")) Type = "river";
			else if (water_tag && !strcmp(water_tag, "canal") && 
							 natural_tag && !strcmp(natural_tag, "water")) Type = "canal";
			else if (waterway_tag && !strcmp(waterway_tag, "riverbank")) Type = "river";
			else if (waterway_tag && !strcmp(waterway_tag, "dock")) Type = "water";
			else if (landuse_tag && !strcmp(landuse_tag, "reservoir")) Type = "reservoir";
			else if (natural_tag && !strcmp(natural_tag, "water")) Type = "water";

			if (Type)
			{
				const char* Name = 0;
				if (area.tags()["int_name"]) Name = area.tags()["int_name"];
				else if (area.tags()["name:en"]) Name = area.tags()["name:en"];
				else if (area.tags()["name"]) Name = area.tags()["name"];
				
				int Im = 0;
				const char* intermittent_tag = area.tags()["intermittent"];
				if (intermittent_tag)
				{
					if (strcmp(intermittent_tag, "no"))
						if (strcmp(intermittent_tag, "0"))
							Im = 1;
				}
				else if (water_tag && !strcmp(water_tag, "intermittent")) Im = 1;

				int Salt = 0;
				const char* salt_tag = area.tags()["salt"];
				if (salt_tag)
				{
					if (strcmp(salt_tag, "no"))
						if (strcmp(salt_tag, "0"))
							Salt = 1;
				}

				int Maritime = 0;
				const char* maritime_tag = area.tags()["maritime"];
				if (maritime_tag)
				{
					if (strcmp(maritime_tag, "no"))
						if (strcmp(maritime_tag, "0"))
							Maritime = 1;
				}

				const char* tidal_tag = area.tags()["tidal"];
				if (tidal_tag)
				{
					if (strcmp(tidal_tag, "no"))
						if (strcmp(tidal_tag, "0"))
							Maritime = 1;
				}

				int FType = 0;
				if (!area.from_way()) FType = 1;

				try {
					gdalcpp::Feature feature(m_layer_polygon, m_factory.create_multipolygon(area));
					feature.set_field("id", static_cast<double>(area.id()));
					feature.set_field("type", Type);
					feature.set_field("ftype", FType);
					feature.set_field("intermittent", Im);
					feature.set_field("salt", Salt);
					feature.set_field("maritime", Maritime);
					if (Name)
						feature.set_field("name", Name);
					feature.add_to_layer();
				} catch (osmium::geometry_error&) {
					std::cerr << "Ignoring illegal geometry for area "
										<< area.id()
										<< " created from "
										<< (area.from_way() ? "way" : "relation")
										<< " with id="
										<< area.orig_id() << ".\n";
				}
			}
    }

};

/* ================================================== */

void print_help() {
    std::cout << "osmium_waterareas [OPTIONS] [INFILE [OUTFILE]]\n\n" \
              << "If INFILE is not given stdin is assumed.\n" \
              << "If OUTFILE is not given 'ogr_out' is used.\n" \
              << "\nOptions:\n" \
              << "  -h, --help             This help message\n" \
              << "  -d, --debug=level      Enable debug output\n" \
              << "  -i, --index=INDEX_TYPE Set index type for location index\n" \
              << "  -f, --format=FORMAT    Output OGR format (Default: 'SQLite')\n";
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"help",   no_argument, 0, 'h'},
        {"debug",  optional_argument, 0, 'd'},
        {"format", required_argument, 0, 'f'},
        {"index",  required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    std::string location_index_type = "dense_mmap_array";
    const auto& map_factory = osmium::index::MapFactory<osmium::unsigned_object_id_type, osmium::Location>::instance();

    std::string output_format{"SQLite"};
    bool debug = false;

    while (true) {
        int c = getopt_long(argc, argv, "hdf:i:", long_options, 0);
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'h':
                print_help();
                exit(0);
            case 'd':
                debug = true;
                break;
            case 'f':
                output_format = optarg;
                break;
            case 'i':
                location_index_type = optarg;
                break;
            default:
                exit(1);
        }
    }

    std::string input_filename;
    std::string output_filename{"ogr_out"};
    int remaining_args = argc - optind;
    if (remaining_args > 2) {
        std::cerr << "Usage: " << argv[0] << " [OPTIONS] [INFILE [OUTFILE]]" << std::endl;
        exit(1);
    } else if (remaining_args == 2) {
        input_filename =  argv[optind];
        output_filename = argv[optind+1];
    } else if (remaining_args == 1) {
        input_filename =  argv[optind];
    } else {
        input_filename = "-";
    }

    osmium::area::Assembler::config_type assembler_config;
    if (debug) {
        assembler_config.debug_level = 1;
    }
    osmium::area::MultipolygonCollector<osmium::area::Assembler> collector{assembler_config};

    std::cerr << "Pass 1...\n";
    osmium::io::Reader reader1{input_filename};
    collector.read_relations(reader1);
    reader1.close();
    std::cerr << "Pass 1 done\n";

    auto location_index = map_factory.create_map(location_index_type);
    location_handler_type location_handler(*location_index);
    location_handler.ignore_errors();

    // Choose one of the following:

    // 1. Use WGS84, do not project coordinates.
    //osmium::geom::OGRFactory<> factory {};

    // 2. Project coordinates into "Web Mercator".
    osmium::geom::OGRFactory<osmium::geom::MercatorProjection> factory;

    // 3. Use any projection that the proj library can handle.
    //    (Initialize projection with EPSG code or proj string).
    //    In addition you need to link with "-lproj" and add
    //    #include <osmium/geom/projection.hpp>.
    //osmium::geom::OGRFactory<osmium::geom::Projection> factory {osmium::geom::Projection(3857)};

    CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "OFF");
    gdalcpp::Dataset dataset{output_format, output_filename, gdalcpp::SRS{factory.proj_string()}, { "SPATIALITE=TRUE", "INIT_WITH_EPSG=no" }};
    MyOGRHandler<decltype(factory)::projection_type> ogr_handler{dataset, factory};

    std::cerr << "Pass 2...\n";
    osmium::io::Reader reader2{input_filename};

    osmium::apply(reader2, location_handler, ogr_handler, collector.handler([&ogr_handler](const osmium::memory::Buffer& area_buffer) {
        osmium::apply(area_buffer, ogr_handler);
    }));

    reader2.close();
    std::cerr << "Pass 2 done\n";

    std::vector<const osmium::Relation*> incomplete_relations = collector.get_incomplete_relations();
    if (!incomplete_relations.empty()) {
        std::cerr << "Warning! Some member ways missing for these multipolygon relations:";
        for (const auto* relation : incomplete_relations) {
            std::cerr << " " << relation->id();
        }
        std::cerr << "\n";
    }

}
