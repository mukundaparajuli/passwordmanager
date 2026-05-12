/**
 * Icon utility module for managing SVG icons
 * Provides a centralized way to create icon elements from SVG files
 * Works in both module and global contexts
 */

const Icons = (() => {
  /**
   * Inline SVG definitions for all icons
   * Keep SVG definitions in one place for easy maintenance
   */
  const inlineSvgs = {
    lock: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
      <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
      <circle cx="12" cy="16" r="1"></circle>
    </svg>`,
    "chevron-right": `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <polyline points="9 18 15 12 9 6"></polyline>
    </svg>`,
    key: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <circle cx="7.5" cy="15.5" r="5.5"></circle>
      <path d="m21 2-9.6 9.6"></path>
      <path d="m15.5 7.5 3 3L22 7l-3-3"></path>
    </svg>`,
    menu: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor">
      <circle cx="12" cy="5" r="2"></circle>
      <circle cx="12" cy="12" r="2"></circle>
      <circle cx="12" cy="19" r="2"></circle>
    </svg>`
  };

  /**
   * Create an icon element synchronously (uses inline SVG)
   * @param {string} iconName - Name of the icon
   * @param {Object} options - Configuration options
   * @param {string} options.width - Width of the icon (default: 24)
   * @param {string} options.height - Height of the icon (default: 24)
   * @param {string} options.color - Color of the icon
   * @param {string} options.className - CSS class name
   * @param {string} options.style - Additional inline styles
   * @returns {SVGElement|null} SVG element
   */
  function createIconSync(iconName, options = {}) {
    const {
      width = "24",
      height = "24",
      color = undefined,
      className = "",
      style = ""
    } = options;

    const svgContent = inlineSvgs[iconName];
    if (!svgContent) {
      console.warn(`Icon not found: ${iconName}`);
      return null;
    }

    const container = document.createElement("div");
    container.innerHTML = svgContent;
    const svgElement = container.querySelector("svg");

    if (!svgElement) return null;

    // Set dimensions
    svgElement.setAttribute("width", width);
    svgElement.setAttribute("height", height);

    // Set color if provided
    if (color) {
      svgElement.style.color = color;
    }

    // Apply className
    if (className) {
      svgElement.setAttribute("class", className);
    }

    // Apply additional styles
    if (style) {
      svgElement.setAttribute("style", style);
    }

    return svgElement;
  }

  return {
    createIconSync
  };
})();

// Export as ES6 module
export default Icons;

// Also make Icons available globally for content scripts and non-module contexts
window.Icons = Icons;
