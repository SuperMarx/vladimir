#include "scraper.hpp"

#include <queue>
#include <jsoncpp/json/json.h>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

#include <supermarx/util/stubborn.hpp>

#include <vladimir/parsers/category_parser.hpp>

namespace supermarx
{

scraper::scraper(product_callback_t _product_callback, tag_hierarchy_callback_t, unsigned int _ratelimit, bool _cache, bool)
	: product_callback(_product_callback)
	, dl("supermarx vladimir/0.2", _ratelimit, _cache ? boost::optional<std::string>("./cache") : boost::none)
	, m(dl, [&]() { error_count++; })
	, product_count(0)
	, page_count(0)
	, error_count(0)
{}

Json::Value parse_json(std::string const& src)
{
	Json::Value root;
	Json::Reader reader;

	if(!reader.parse(src, root, false))
		throw std::runtime_error("Could not parse json feed");

	return root;
}

void scraper::schedule_submenu(category const& c_parent)
{
	m.schedule(c_parent.url, [=](downloader::response const& response) {
		page_count++;

		category_parser cp([&](category const& /*c*/) {
			// Do not execute schedule_submenu(c);
			// All products are yielded on this level
		}, [&](size_t id) {
			process_products(c_parent, id);
		});

		cp.parse(response.body);
	});
}

void report_problem_understanding(std::string const& field, std::string const& value, scraper::problems_t& problems)
{
	std::stringstream sstr;
	sstr << "Unclear '" << field << "' with value '" << value << "'";

	problems.emplace_back(sstr.str());
}

void interpret_unit(std::string unit, uint64_t& volume, measure& volume_measure, confidence& conf, scraper::problems_t& problems)
{
	static const std::set<std::string> valid_stuks({
		"st", "stuk", "stuks",
		"scharreleieren", "tabl", "filterhulzen", "test", "schuursponsen",
		"verbanden", "tandenstokers", "strips", "bruistabletten",
		"patches", "tabletten", "condooms", "capsules", "servetten", "paar",
		"gezichtstissues", "rol", "tissues", "sigaretten", "witte bollen",
		"haardblok", "vrije uitloop eieren", "braadschotels", "caps", "geurkaarsen",
		"luiers", "kauwtabletten", "pakjes", "tampons", "blaarpleisters",
		"rollen", "toiletrollen", "aanstekers", "geurbuiltjes", "slim filters",
		"theefilters", "inlegkruisjes", "sigaren", "beschuiten", "batterijen",
		"doekjes", "sigaretten"
	});

	static const std::set<std::string> invalid_stuks({
		"wasbeurten", "plakjes", "bos", "bosjes", "porties",
		"zakjes", "sachets", "kaarten", "lapjes", "zegel", "pakjes"
	});

	static const boost::regex match_multi("([0-9]+)(?: )?[xX](?: )?(.+)");

	static const boost::regex match_stuks("(?:ca. )?([0-9]+) stuk\\(s\\)");
	static const boost::regex match_pers("(?:ca. )?([0-9]+(?:-[0-9]+)?) pers\\.");

	static const boost::regex match_measure("(?:ca. )?([0-9]+(?:\\.[0-9]+)?)(?: )?(mg|g|gr|gram|kg|kilo|ml|cl|l|lt|liter|litre|m)(?:\\.)?");
	boost::smatch what;

	std::replace(unit.begin(), unit.end(), ',', '.');

	uint64_t multiplier = 1;
	if(boost::regex_match(unit, what, match_multi))
	{
		multiplier = boost::lexical_cast<uint64_t>(what[1]);
		unit = what[2];
	}

	if(
		unit == "per stuk" ||
		unit == "per krop" ||
		unit == "per bos" ||
		unit == "per bosje" ||
		unit == "per doos" ||
		unit == "per set" ||
		unit == "éénkops" ||
		boost::regex_match(unit, what, match_pers)
	)
	{
		// Do nothing
	}
	else if(boost::regex_match(unit, what, match_stuks))
	{
		if(valid_stuks.find(what[2]) != valid_stuks.end())
			volume = boost::lexical_cast<float>(what[1]);
		else if(invalid_stuks.find(what[2]) != invalid_stuks.end())
		{
			// Do nothing
		}
	}
	else if(boost::regex_match(unit, what, match_measure))
	{
		std::string measure_type = what[2];

		if(measure_type == "mg")
		{
			volume = boost::lexical_cast<float>(what[1]);
			volume_measure = measure::MILLIGRAMS;
		}
		else if(measure_type == "g" || measure_type == "gr" || measure_type == "gram")
		{
			volume = boost::lexical_cast<float>(what[1])*1000.0f;
			volume_measure = measure::MILLIGRAMS;
		}
		else if(measure_type == "kg" || measure_type == "kilo")
		{
			volume = boost::lexical_cast<float>(what[1])*1000000.0f;
			volume_measure = measure::MILLIGRAMS;
		}
		else if(measure_type == "ml")
		{
			volume = boost::lexical_cast<float>(what[1]);
			volume_measure = measure::MILLILITRES;
		}
		else if(measure_type == "cl")
		{
			volume = boost::lexical_cast<float>(what[1])*100.0f;
			volume_measure = measure::MILLILITRES;
		}
		else if(measure_type == "l" || measure_type == "lt" || measure_type == "liter" || measure_type == "litre")
		{
			volume = boost::lexical_cast<float>(what[1])*1000.0f;
			volume_measure = measure::MILLILITRES;
		}
		else if(measure_type == "m")
		{
			volume = boost::lexical_cast<float>(what[1])*1000.0f;
			volume_measure = measure::MILLIMETRES;
		}
		else
		{
			report_problem_understanding("measure_type", measure_type, problems);
			conf = confidence::LOW;
			return;
		}
	}
	else
	{
		report_problem_understanding("unit", unit, problems);
		conf = confidence::LOW;
	}

	volume *= multiplier;
}

void scraper::process_products(category const& c, size_t id)
{
	std::string uri("https://www.coop.nl/actions/ViewAjax-Start?PageNumber=0&PageSize=100000&TargetPipeline=ViewStandardCatalog-ProductPaging&CatalogID=COOP&CategoryName=");
	uri += boost::lexical_cast<std::string>(id);

	m.schedule(uri, [=](downloader::response const& response) {
		page_count++;

		Json::Value root(parse_json(response.body));
		for(auto const& j : root)
		{
			std::vector<std::string> problems;

			message::product_base p;
			confidence conf = confidence::NEUTRAL;
			datetime retrieved_on = datetime_now();

			p.tags.push_back({c.name, std::string("category")});

			p.identifier = j["productSKU"].asString();

			p.name = j["productName"].asString();

			p.valid_on = retrieved_on;
			p.discount_amount = 1;

			p.orig_price =
						boost::lexical_cast<size_t>(j["standardPrice"]["whole"].asString())*100 +
						boost::lexical_cast<size_t>(j["standardPrice"]["fraction"].asString());
			if(!j["salePrice"].isNull()) {
				p.price =
							boost::lexical_cast<size_t>(j["salePrice"]["whole"].asString())*100 +
							boost::lexical_cast<size_t>(j["salePrice"]["fraction"].asString());
			} else {
				p.price = p.orig_price;
			}

			p.volume = 1;
			p.volume_measure = measure::UNITS;

			interpret_unit(j["productSubText"].asString(), p.volume, p.volume_measure, conf, problems);

			static const boost::regex match_percent_discount("([0-9]+)% (?:probeer)?korting");
			static const boost::regex match_combination_discount("([0-9]+) voor ([0-9]+).([0-9]+)");
			static const boost::regex match_fixed("nu voor ([0-9]+).([0-9]+)");
			static const boost::regex match_volume_discount("([0-9]+).([0-9]+) per 100 gram");
			boost::smatch what;

			std::string sticker = j["productStickerText"].asString();
			std::transform(sticker.begin(), sticker.end(), sticker.begin(), ::tolower);

			if(sticker == "" || sticker == "actie" || sticker == "per 100 gram") {
				// Do nothing
			} else if(boost::regex_match(sticker, what, match_combination_discount)) {
				p.discount_amount = boost::lexical_cast<uint64_t>(what[1]);
				p.price = boost::lexical_cast<float>(what[2] + '.' + what[3])*100.0f;
			} else if(boost::regex_match(sticker, what, match_volume_discount) && p.volume_measure == measure::MILLIGRAMS) {
				p.price = boost::lexical_cast<float>(what[1] + '.' + what[2])*100.0f * (p.volume / 100000.0f);
			} else if(boost::regex_match(sticker, what, match_fixed)) {
				p.price = boost::lexical_cast<float>(what[1] + '.' + what[2])*100.0f;
			} else if(boost::regex_match(sticker, what, match_percent_discount)) {
				p.price *= 1.0f - boost::lexical_cast<float>(what[1])/100.0f;
			} else if(sticker == "1 + 1 gratis") {
				p.discount_amount = 2;
				p.price *= 0.5;
			} else if(sticker == "2 + 1 gratis") {
				p.discount_amount = 3;
				p.price *= (3.0 / 2.0);
			} else {

				report_problem_understanding("productStickerText", sticker, problems);
				conf = confidence::LOW;
			}

			boost::optional<std::string> image_uri;
			if(j["image540"].isString()) {
				image_uri = j["image540"].asString();
			}

			product_count++;
			product_callback(uri, image_uri, p, retrieved_on, conf, problems);
		}
	});
}

void scraper::scrape()
{
	product_count = 0;
	page_count = 0;
	error_count = 0;

	category_parser cp(
		[&](category const& c) {
			schedule_submenu(c);
		},
		[](size_t) {}
	);

	cp.parse(stubborn::attempt<std::string>([&]() {
		return dl.fetch("https://www.coop.nl/boodschappen").body;
	}));

	m.process_all();
	std::cerr << "Pages: " << page_count << ", products: " << product_count << ", errors: " << error_count << std::endl;
}

raw scraper::download_image(const std::string& uri)
{
	std::string buf(dl.fetch(uri).body);
	return raw(buf.data(), buf.length());
}

}
