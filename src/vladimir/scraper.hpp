#pragma once

#include <functional>

#include <supermarx/product.hpp>
#include <supermarx/util/downloader.hpp>

namespace supermarx
{
	class scraper
	{
	public:
		using problems_t = std::vector<std::string>;
		using callback_t = std::function<void(const product&, datetime, confidence, problems_t)>;

		struct category
		{
			uint64_t id;
			std::string name;
			bool has_children;
		};

		using cat_callback_t = std::function<void(category const&)>;

	private:
		callback_t callback;
		downloader dl;

		void get_rootmenu(cat_callback_t const& f);
		void get_submenu(category const& c, cat_callback_t const& f);
		void process_products(category const& c);

	public:
		scraper(callback_t _callback, unsigned int ratelimit = 5000);
		scraper(scraper&) = delete;
		void operator=(scraper&) = delete;

		void scrape();
	};
}
