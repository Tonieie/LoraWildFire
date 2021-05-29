var margin = { top: 10, right: 30, bottom: 30, left: 60 },
    width = 460 - margin.left - margin.right,
    height = 400 - margin.top - margin.bottom;

// append the svg object to the body of the page
//Read the data
d3.json("https://lorawildfire-default-rtdb.asia-southeast1.firebasedatabase.app/.json",

    function (data) {
        draw(data, "node1")
    });

function draw(data, node) {
    var data = data[node];

    data.forEach(function (d) {
        d.timestamp = new Date(d.timestamp);
        d.temp = +d.temp;
    });

    var zoom = d3.zoom()
        .scaleExtent([.5, 20])  // This control how much you can unzoom (x0.5) and zoom (x20)
        .extent([[0, 0], [width, height]])
        .on("zoom", updateChart);

    var svg = d3.select("#my_dataviz")
        .append("svg")
        .attr("width", width + margin.left + margin.right)
        .attr("height", height + margin.top + margin.bottom)
        .call(zoom)
        .append("g")
        .attr("transform",
            "translate(" + margin.left + "," + margin.top + ")");

    var x = d3.scaleTime()
        .domain(d3.extent(data, function (d) { return d.timestamp; }))
        // .domain([Date.now() - 24 * 60 * 60 * 1000,Date.now()])
        .nice()
        .range([0, width]);
    xAxis = svg.append("g")
        .attr("transform", "translate(0," + height + ")")
        .call(d3.axisBottom(x));

    // Add Y axis
    var y = d3.scaleLinear()
        .domain([0, d3.max(data, function (d) { return +d.temp; }) + 2])
        .range([height, 0]);
    yAxis = svg.append("g")
        .call(d3.axisLeft(y));

    var clip = svg.append("defs").append("svg:clipPath")
        .attr("id", "clip")
        .append("svg:rect")
        .attr("width", width)
        .attr("height", height)
        .attr("x", 0)
        .attr("y", 0);
        
    // create a tooltip
    var Tooltip = d3.select("#my_dataviz")
        .append("div")
        .style("opacity", 0)
        .attr("class", "tooltip")
        .style("background-color", "white")
        .style("border", "solid")
        .style("border-width", "2px")
        .style("border-radius", "5px")
        .style("padding", "5px")

    // Three function that change the tooltip when user hover / move / leave a cell
    var mouseover = function (d) {
        Tooltip
            .style("opacity", 1)
    }
    var mousemove = function (d) {
        Tooltip
            .html("Exact value: " + d.temp + "<br>" + d3.timeFormat("%X<br> %e %b %Y")(d.timestamp) + "</br>")
            .style("left", d3.mouse(this)[0] + 80 + "px")
            .style("top", d3.mouse(this)[1] + 10 + "px")
            .style("position", "absolute")
    }
    var mouseleave = function (d) {
        Tooltip
            .style("opacity", 0)
    }

    var scatter = svg.append("g")
        .attr("clip-path", "url(#clip)")

    scatter.append("g")
        .append("path")
        .datum(data)
        .attr("fill", "none")
        .attr("stroke", "steelblue")
        .attr("stroke-width", 2)
        .attr("d", d3.line()
            // .curve(d3.curveBasis) // Just add that to have a curve instead of segments
            .x(function (d) { return x(d.timestamp) })
            .y(function (d) { return y(d.temp) })
        )

    scatter
        .append("g")
        .selectAll("dot")
        .data(data)
        .enter()
        .append("circle")
        .attr("class", "myCircle")
        .attr("cx", function (d) { return x(d.timestamp) })
        .attr("cy", function (d) { return y(d.temp) })
        .attr("r", 6)
        .attr("stroke", "#00A88F")
        .attr("stroke-width", 2)
        .attr("fill", "white")
        .on("mouseover", mouseover)
        .on("mousemove", mousemove)
        .on("mouseleave", mouseleave);



    function updateChart() {

        // recover the new scale
        var newX = d3.event.transform.rescaleX(x);
        var newY = d3.event.transform.rescaleY(y);

        // update axes with these new boundaries
        xAxis.call(d3.axisBottom(newX))
        // yAxis.call(d3.axisLeft(newY))

        // update circle position
        scatter
            .selectAll("circle")
            .attr('cx', function (d) { return newX(d.timestamp) })
            .attr('cy', function (d) { return y(d.temp) });

        scatter
            .selectAll("path")
            .datum(data)
            .attr("d", d3.line()
                // .curve(d3.curveBasis) // Just add that to have a curve instead of segments
                .x(function (d) { return newX(d.timestamp) })
                .y(function (d) { return y(d.temp) })
            )

    }

}