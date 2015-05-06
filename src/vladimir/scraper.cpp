#include "scraper.hpp"

#include <queue>
#include <jsoncpp/json/json.h>
#include <boost/algorithm/string.hpp>

#include <supermarx/util/stubborn.hpp>

namespace supermarx
{

Json::Value parse_json(std::string const& src)
{
	Json::Value root;
	Json::Reader reader;

	if(!reader.parse(src, root, false))
		throw std::runtime_error("Could not parse json feed");

	return root;
}

scraper::scraper(callback_t _callback, unsigned int _ratelimit)
	: callback(_callback)
	, dl("supermarx vladimir/0.1", _ratelimit)
{}

void scraper::get_rootmenu(cat_callback_t const& f)
{
	Json::Value root(parse_json(
		stubborn::attempt<std::string>([&]{
			return dl.fetch("https://api-01.cooponline.nl/shopapi/webshopCategory/getMenu");
		})
	));

	for(auto const& cat : root["rootWebshopCategories"])
	{
		category c({cat["id"].asUInt64(), cat["name"].asString(), cat["hasChildren"].asBool()});
		f(c);
	}
}

void scraper::get_submenu(category const& c, cat_callback_t const& f)
{
	std::string uri("https://api-01.cooponline.nl/shopapi/webshopCategory/getMenu?categoryId=");
	uri += boost::lexical_cast<std::string>(c.id);

	Json::Value root(parse_json(
		stubborn::attempt<std::string>([&]{
			return dl.fetch(uri);
		})
	));

	for(auto const& cat : root["childrenWebshopCategories"])
	{
		category c({cat["id"].asUInt64(), cat["name"].asString(), cat["hasChildren"].asBool()});
		f(c);
	}
}

void report_problem_understanding(std::string const& field, std::string const& value, scraper::problems_t& problems)
{
	std::stringstream sstr;
	sstr << "Unclear '" << field << "'' with value '" << value << "'";

	problems.emplace_back(sstr.str());
}

void scraper::process_products(category const& c)
{
	std::vector<std::string> problems;

	std::string uri("https://api-01.cooponline.nl/shopapi/article/list?offset=0&size=10000&webshopCategoryId=");
	uri += boost::lexical_cast<std::string>(c.id);

	Json::Value root(parse_json(
		stubborn::attempt<std::string>([&]{
			return dl.fetch(uri);
		})
	));

	for(auto const& j : root["articles"])
	{
		product p;
		confidence conf = confidence::NEUTRAL;
		datetime retrieved_on = datetime_now();

		p.identifier = j["articleNumber"].asString();

		std::string brand = j["brand"]["name"].asString();
		if(brand == "-------")
			brand = "coop";

		p.name = brand + " " + j["name"].asString();

		// Coop gives names as uppercase strings, which is undesireable.
		boost::algorithm::to_lower(p.name, std::locale("en_US.utf8")); // TODO fix UTF8-handling with ICU or similar.

		p.valid_on = retrieved_on;
		p.discount_amount = 1;

		p.price = j["salePrice"].asFloat()*100.0;
		if(!j["originalPrice"].isNull())
			p.orig_price = j["originalPrice"].asFloat()*100.0;
		else
			p.orig_price = p.price;

		if(j["mixMatched"].asBool())
		{
			std::string type = j["mixMatchButtonType"].asString();
			if(type == "FIXED_PRICE")
			{
				p.price = j["mixMatchDiscount"].asFloat()*100.0;
			}
			else if(type == "QUANTITY_FIXED_PRICE")
			{
				p.discount_amount = j["mixMatchItemQuantity"].asInt();
				p.price = j["mixMatchDiscount"].asFloat()*100.0 / p.discount_amount;
			}
			else if(type == "PERCENT")
			{
				p.discount_amount = j["mixMatchItemQuantity"].asInt();
				p.price *= j["mixMatchDiscount"].asFloat()/100.0;
			}
			else if(type == "TWO_BUY_ONE_PAY")
			{
				p.discount_amount = j["mixMatchItemQuantity"].asInt();
				p.price *= j["mixMatchDiscount"].asFloat()/100.0;
			}
			else if(type == "" && j["mixMatchDiscount"].isInt() && j["mixMatchDiscount"].asInt() == 100)
			{
				// Not actually a discount, bug in Coop's system
			}
			else
			{
				report_problem_understanding("mixMatchButtonType", type, problems);
				conf = confidence::LOW;
			}
		}

		const static std::map<std::string, std::pair<uint64_t, measure>> measure_map({
			{"DOZIJN", {12, measure::UNITS}},
			{"GROS", {144, measure::UNITS}},
			{"MILIGRAM", {1, measure::MILLIGRAMS}},
			{"GRAM", {1000, measure::MILLIGRAMS}},
			{"HECTOGRM", {100000, measure::MILLIGRAMS}},
			{"KILOGRAM", {1000000, measure::MILLIGRAMS}},
			{"TON", {1000000000, measure::MILLIGRAMS}},
			{"POND", {500000, measure::MILLIGRAMS}},
			{"ONS", {100000, measure::MILLIGRAMS}},
			{"MILIMETR", {1, measure::MILLIMETERS}},
			{"CENTIMTR", {10, measure::MILLIMETERS}},
			{"DECIMETR", {100, measure::MILLIMETERS}},
			{"METER", {1000, measure::MILLIMETERS}},
			{"KILOMETR", {1000000, measure::MILLIMETERS}},
			{"MILILITR", {1, measure::MILLILITERS}},
			{"CENTILTR", {10, measure::MILLILITERS}},
			{"DECILITR", {100, measure::MILLILITERS}},
			{"LITER", {1000, measure::MILLILITERS}},
			{"DECALITR", {10000, measure::MILLILITERS}},
			{"HECTOLTR", {100000, measure::MILLILITERS}},
			{"STUK", {1, measure::UNITS}},
			{"Diversen", {1, measure::UNITS}},
			{"PLAK", {1, measure::UNITS}},
			{"PUNT(EN)", {1, measure::UNITS}},
			{"ROL(LEN)", {1, measure::UNITS}}
		});

		p.volume = 1;
		p.volume_measure = measure::UNITS;

		std::string volume_str = j["volume"].asString();
		std::string measure_str = j["volumeMeasure"]["name"].asString();

		auto measure_it = measure_map.find(measure_str);
		if(measure_it != measure_map.end())
		{
			try
			{
				p.volume = boost::lexical_cast<uint64_t>(volume_str) * measure_it->second.first;
				p.volume_measure = measure_it->second.second;
			} catch(boost::bad_lexical_cast e)
			{
				report_problem_understanding("volume", volume_str, problems);
				conf = confidence::LOW;
			}
		}
		else
		{
			report_problem_understanding("volumeMeasure", volume_str, problems);
			conf = confidence::LOW;
		}

		callback(p, retrieved_on, conf, problems);
	}
}

void scraper::scrape()
{
	std::vector<category> todo;

	cat_callback_t cat_f = [&](category const& c) {
		todo.push_back(c);
	};

	get_rootmenu(cat_f);

	while(!todo.empty())
	{
		category c(todo.back());
		todo.pop_back();

		if(c.has_children)
			get_submenu(c, cat_f);

		process_products(c);
	}
}

}
