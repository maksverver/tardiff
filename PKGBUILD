pkgname=tardiff
pkgver=1.0
pkgrel=2
pkgdesc="Binary diff and patch tools"
arch=('i686' 'x86_64')
url="https://github.com/maksverver/tardiff"
license=('MIT')
depends=()

build() {
	make -C "${startdir}"
}

package() {
	mkdir -p "${pkgdir}/usr/bin"
	make -C "${startdir}" PREFIX="${pkgdir}/usr" install
	install -Dm644 "${startdir}/LICENSE" "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
}
