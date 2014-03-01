FeatherAffinity
===============

A command-line bot that posts random content from FurAffinity to a Twitter account at random times, with tag, rating, and artist filtering.

Compiling
---------

FeatherAffinity uses the C++11 standard, and has the following dependencies:

libcurl, for retrieving pages;

libhtmlcxx, for scraping the pages;

liboauth, for twitter authentication.

Configuration
-------------

A sample config file "featherconfig.cfg" is provided, but the values must be replaced with those specific to the user's accounts before it will work.

An app profile must be created for each instance through Twitter's developer panel, and the oauth tokens and keys can be retrieved from within that panel.

The log level has three possible values: 0, for critical errors only; 1, to include warnings; and 2, to include normal messages and information from the program's operation.

Using FeatherAffinity
---------------------

Logs are sent to stdout, and can easily be redierected to a file.

Posted submission IDs are stored in "submissions.csv", so that submissions are only shown once. A cURL cookiejar file "FAcookie.tsv" is created to maintain FA session information during use.

Submissions can be filtered by creating a file named "block.csv" to block specific tags and/or a file named "require.csv" to require specific tags. Each line should contain one (case-insensitive) tag, followed by a comma. Sample files have been provided, though they are not required for FeatherAffinity to run.

Ratings and artist usernames are also counted as tags, so it is possible to require or block SFW/NSFW submissions and/or those by specific artists in addition to filtering by the submission tags. However, remember that the more specific your filters are the longer it will take to find a valid submission. The amount of required tags greatly influences this, while the amount of blocked tags has a significantly lesser effect.
