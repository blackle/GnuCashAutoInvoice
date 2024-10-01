This mess of code was written to automatically generate and email invoices for each member's monthly fees.

Each member is a "customer" in gnucash's database, and their "notes" field is data about when their membership started and ended (there can be multiple start and end dates) as well as their dues during that time. this is encoded as a json array. for example, for someone who joined for a few months at $10/month, then left, then joined again at a higher rate of $25/month, their notes would look like this:

{
	{ "dues": "10.00", "start": "1-10-2023", "end": "10-12-2023" },
	{ "dues": "25.00", "start": "5-4-2024", "end": "" },
]

date format is D-M-Y.

To build this, you need to make gnucash-5.4 from source, and set the install prefix to the same directory as the source. for example:

GNUCASH_PREFIX=/home/blackle/Downloads/gnucash

/home/blackle/Downloads/gnucash/gnucash-5.4
/home/blackle/Downloads/gnucash/share
/home/blackle/Downloads/gnucash/lib
/home/blackle/Downloads/gnucash/include
...

# install the wkhtmltopdf cli tool
sudo apt install wkhtmltopdf
cmake -DGNUCASH_PREFIX=/home/blackle/Downloads/gnucash -DGNUCASH_BUILD=/home/blackle/Downloads/gnucash/gnucash-5.4 -B build .
cmake --build build
./build/auto_invoice --your-arguments

After running auto_invoice, it will generate a file called "email_jobs.tsv". This is a list of emails and their corresponding invoice PDFs. You can write some other software to send these using your own email provider.