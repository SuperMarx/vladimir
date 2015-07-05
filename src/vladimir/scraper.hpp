#pragma once

#include <functional>

#include <supermarx/message/product_base.hpp>
#include <supermarx/message/tag.hpp>

#include <supermarx/raw.hpp>
#include <supermarx/util/cached_downloader.hpp>

namespace supermarx
{
	class scraper
	{
	public:
		using problems_t = std::vector<std::string>;
		using callback_t = std::function<void(std::string const&, boost::optional<std::string> const&, const message::product_base&, std::vector<message::tag> const&, datetime, confidence, problems_t)>;

		struct category
		{
			uint64_t id;
			std::string name;
			bool has_children;
		};

		using cat_callback_t = std::function<void(category const&)>;

	private:
		callback_t callback;
		cached_downloader dl;

		void get_rootmenu(cat_callback_t const& f);
		void get_submenu(category const& c, cat_callback_t const& f);
		void process_products(category const& c);

	public:
		scraper(callback_t _callback, unsigned int ratelimit = 5000, bool cache = false, bool register_tags = false);
		scraper(scraper&) = delete;
		void operator=(scraper&) = delete;

		void scrape();
		raw download_image(const std::string& uri);
	};
}
