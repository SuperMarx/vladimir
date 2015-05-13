#include <supermarx/scraper/scraper_cli.hpp>
#include <vladimir/scraper.hpp>

int main(int argc, char** argv)
{
	return supermarx::scraper_cli<supermarx::scraper>::exec(2, "vladimir", "Coop", argc, argv);
}
