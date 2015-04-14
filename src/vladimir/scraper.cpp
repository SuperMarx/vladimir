#include "scraper.hpp"

#include <queue>
#include <jsoncpp/json/json.h>
#include <boost/algorithm/string.hpp>

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
	Json::Value root(parse_json(dl.fetch("https://api-01.cooponline.nl/shopapi/webshopCategory/getMenu")));

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

	Json::Value root(parse_json(dl.fetch(uri)));
	for(auto const& cat : root["childrenWebshopCategories"])
	{
		category c({cat["id"].asUInt64(), cat["name"].asString(), cat["hasChildren"].asBool()});
		f(c);
	}
}

void scraper::process_products(category const& c)
{
	std::cerr << "Fetching products for " << c.name << " [" << c.id << ']' << std::endl;
	std::string uri("https://api-01.cooponline.nl/shopapi/article/list?offset=0&size=10000&webshopCategoryId=");
	uri += boost::lexical_cast<std::string>(c.id);

	Json::Value root(parse_json(dl.fetch(uri)));
	for(auto const& j : root["articles"])
	{
		product p;
		confidence conf = confidence::NEUTRAL;
		datetime retrieved_on = datetime_now();

		p.identifier = j["articleNumber"].asString();
		p.name = j["brand"]["name"].asString() + " " + j["name"].asString();

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
			else
			{
				std::cerr << '[' << p.identifier << "] " << p.name << std::endl;
				std::cerr << "mixMatchButtonType: \'" << type << '\'' << std::endl;
				conf = confidence::LOW;
			}
		}

		callback(p, retrieved_on, conf);
	}
}

void scraper::scrape()
{
	std::vector<category> todo;

	cat_callback_t cat_f = [&](category const& c) {
		std::cerr << "Retrieving categories for " << c.name << " [" << c.id << ']' << std::endl;
		todo.push_back(c);

		if(c.has_children)
			get_submenu(c, cat_f);
	};

	get_rootmenu(cat_f);

	while(!todo.empty())
	{
		category c(todo.back());
		todo.pop_back();

		process_products(c);
	}
}

}
