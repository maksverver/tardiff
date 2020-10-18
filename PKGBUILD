pkgname=tardiff
pkgver=1.0
pkgrel=1
pkgdesc="Binary diff and patch tools"
arch=('i686' 'x86_64')
depends=()

build() {
	make -C "${startdir}"
}

package() {
	mkdir -p "${pkgdir}/usr/bin"
	make -C "${startdir}" PREFIX="${pkgdir}/usr" install
}
