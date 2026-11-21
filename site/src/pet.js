/*
 * The hatchling in the hero: every few seconds something drifts in from
 * the edge of its screen and it eats it. Wi-Fi food is mint, Bluetooth
 * food is lilac. Nothing here talks to anything; it is only a picture.
 */
(function () {
  var watch = document.getElementById('hero-watch');
  var pet = document.getElementById('hero-pet');
  if (!watch || !pet) return;
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;

  var menu = [
    ['a coffee shop network', false],
    ["someone's earbuds", true],
    ['a parked car', true],
    ['a printer down the hall', false],
    ['a passing phone', true],
    ['a hidden network', false],
    ['a phone hotspot', false],
    ['a smart TV', true]
  ];
  var next = 0;

  function serve() {
    if (document.hidden) return;
    var item = menu[next++ % menu.length];
    var morsel = document.createElement('span');
    morsel.className = 'morsel' + (item[1] ? ' bluetooth' : '');
    morsel.textContent = item[0];
    // It comes in from one side or the other, from above the pet's head.
    var side = next % 2 ? -1 : 1;
    morsel.style.setProperty('--x', side * 6 + 'rem');
    morsel.style.setProperty('--y', '-11rem');
    watch.appendChild(morsel);
    morsel.classList.add('go');
    setTimeout(function () {
      pet.classList.add('munch');
      setTimeout(function () { pet.classList.remove('munch'); }, 800);
    }, 2100);
    setTimeout(function () { morsel.remove(); }, 2700);
  }

  setTimeout(serve, 900);
  setInterval(serve, 4200);
})();
