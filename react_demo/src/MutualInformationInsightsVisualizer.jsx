;(function (root, factory) {
  root.MutualInformationInsightsVisualizer = factory(
    root.React
  );
})(this, function (React) {
  // A mutual info visualizer implementation.
  return React.createClass({
    render: function () {
      var _this = this;

      // HACK(sompylasar): Simplest error handling.
      try {
        var insightData = _this.props.insightData;

        var insightCounters = insightData.counters;

        // The component must render a single root node.
        return <div className="c5t-mutual-information-insights-visualizer">
          <div style={{ textAlign: 'center', width: '100%' }}>
            <h2>{insightData.lhs}</h2>
            vs.
            <h2>{insightData.rhs}</h2>
          </div>
          <table border="0" align="center" cellSpacing="24">
            <tbody>
              <tr>
                <td>
                  <table border="1" align="center" cellPadding="8">
                    <tbody>
                      <tr align="center">
                        <td></td>
                        <td><b>YES</b><pre>{_this._humanizeExpression(insightData.lhs, true)}</pre></td>
                        <td><b>NO</b><pre>{_this._humanizeExpression(insightData.lhs, false)}</pre></td>
                      </tr>
                      <tr align="center">
                        <td><b>A</b></td>
                        <td><font size="+2"><pre>{insightCounters.lhs}</pre></font></td>
                        <td><font size="+2"><pre>{insightCounters.N - insightCounters.lhs}</pre></font></td>
                      </tr>
                    </tbody>
                  </table>
                </td>
                <td>
                  <table border="1" align="center" cellPadding="8">
                    <tbody>
                      <tr align="center">
                        <td></td>
                        <td><b>YES</b><pre>{_this._humanizeExpression(insightData.rhs, true)}</pre></td>
                        <td><b>NO</b><pre>{_this._humanizeExpression(insightData.rhs, false)}</pre></td>
                      </tr>
                      <tr align="center">
                        <td><b>B</b></td>
                        <td><font size="+2"><pre>{insightCounters.rhs}</pre></font></td>
                        <td><font size="+2"><pre>{insightCounters.N - insightCounters.rhs}</pre></font></td>
                      </tr>
                    </tbody>
                  </table>
                </td>
              </tr>
            </tbody>
          </table>
          <br />
          <table border="1" align="center" cellPadding="8">
            <tbody>
              <tr align="center">
                <td></td>
                <td><b>B: YES</b></td>
                <td><b>B: NO</b></td>
              </tr>
              <tr align="center">
                <td><b>A: YES</b></td>
                <td><font size="+4"><pre>{insightCounters.yy}</pre></font></td>
                <td><font size="+4"><pre>{insightCounters.yn}</pre></font></td>
              </tr>
              <tr align="center">
                <td><b>A: NO</b></td>
                <td><font size="+4"><pre>{insightCounters.ny}</pre></font></td>
                <td><font size="+4"><pre>{insightCounters.nn}</pre></font></td>
              </tr>
            </tbody>
          </table>
        </div>;
      }
      catch (ex) {
        return <div className="c5t-mutual-information-insights-visualizer">
          <div className="c5t-error">
           An error occurred: <span>{ex.message}</span>
          </div>
        </div>;
      }
    },

    /**
     * Makes a human-readable string from an expression like 'DaysInterval>=6'.
     *
     * @param {string} expr The expression.
     * @param {boolean} yes `true` to humanize the original expression, `false` to humanize the inverted one.
     * @return {string}
     */
    _humanizeExpression: function (expr, yes) {
      // Supported operators.
      var ops = {
        // 'operator': [ 'Human readable', 'inverse operator' ]
        '>': [ 'More than {rhs} {var}', '<=' ],
        '<': [ 'Less than {rhs} {var}', '>=' ],
        '>=': [ 'More than or equal to {rhs} {var}', '<' ],
        '<=': [ 'Less than or equal to {rhs} {var}', '>' ]
      };

      // Sort so that longer strings match first.
      var opnames = Object.keys(ops).sort().reverse();

      // re = /(variable name)(one of operators)(variable value)/
      var re = new RegExp('(.+)((?:' + opnames.join(')|(?:') + '))(.+)');

      // Test for comparison expression. Return if humanized.
      var match = re.exec(expr);
      var op;
      if (match && (op = ops[match[2]])) {
        return (yes ? op[0] : ops[op[1]][0] ).replace('{var}', match[1]).replace('{rhs}', match[3]);
      }

      // Test for boolean expression. Return if humanized.
      if (expr.indexOf('Has') === 0) {
        return (yes
          ? expr.replace(/^Has/, 'Has ')
          : expr.replace(/^Has/, 'Does not have ')
        );
      }

      // Cannot humanize.
      return expr;
    }
  });
});