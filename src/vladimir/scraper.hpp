#pragma once

#include <supermarx/raw.hpp>
#include <supermarx/util/cached_downloader.hpp>

#include <supermarx/scraper/scraper_prototype.hpp>

namespace supermarx
{
	class scraper
	{
	public:
		using problems_t = scraper_prototype::problems_t;
		using product_callback_t = scraper_prototype::product_callback_t;
		using tag_hierarchy_callback_t = scraper_prototype::tag_hierarchy_callback_t;

		struct category
		{
			uint64_t id;
			std::string name;
			bool has_children;
		};

		using cat_callback_t = std::function<void(category const&)>;

	private:
		product_callback_t product_callback;
		cached_downloader dl;

		void get_rootmenu(cat_callback_t const& f);
		void get_submenu(category const& c, cat_callback_t const& f);
		void process_products(category const& c);

	public:
		scraper(product_callback_t _product_callback, tag_hierarchy_callback_t _tag_hierarchy_callback, unsigned int ratelimit = 5000, bool cache = false, bool register_tags = false);
		scraper(scraper&) = delete;
		void operator=(scraper&) = delete;

		void scrape();
		raw download_image(const std::string& uri);
	};
}
