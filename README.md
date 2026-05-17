# Webify

A command line tool to convert ttf file to woff, woff2, eot & svg files

## Usage

    $> webify fontname.ttf

For the list of available options

    $> webify --help

## CSS @font-face template

````css
    @font-face {
        font-family: 'my-font-family';
        src: url('my-font-filename.eot');
        src: url('my-font-filename.eot?#iefix') format('embedded-opentype'),
        url('my-font-filename.svg#my-font-family') format('svg'),
        url('my-font-filename.woff') format('woff'),
        url('my-font-filename.woff2') format('woff2'),
        url('my-font-filename.ttf') format('truetype');
        font-weight: normal;
        font-style: normal;
    }
````

## Supported conversion formats

|     | [WOFF 1.0][w1] | [WOFF 2.0][w2] | [SVG][svg] | [EOT][eot] | [MTX][mtx] |
|-----|----------------|----------------|------------|------------|------------|
| TTF |       ✔        |       ✔        |      ✔     |      ✔     |            |
| OTF |        ✔       |        ✔       |      ✔     |      ✔     |            |


[w1]: http://www.w3.org/TR/WOFF/
[w2]: http://www.w3.org/TR/WOFF2/
[svg]: http://www.w3.org/TR/SVG/fonts.html
[eot]: http://www.w3.org/Submission/EOT/
[mtx]: http://www.w3.org/Submission/MTX/
