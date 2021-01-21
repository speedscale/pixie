import setuptools

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setuptools.setup(
    name="pixie",
    version="0.0.1",
    author="Pixie Labs",
    author_email="help@pixielabs.ai",
    description="The python client for the Pixie API",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/pixie-labs/pixie",
    packages=setuptools.find_packages(),
    classifiers=[
        "Programming Language :: Python :: 3",
        "Operating System :: OS Independent",
    ],
    python_requires='>=3.8',
)